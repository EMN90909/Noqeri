#!/usr/bin/env python3
"""Cross-process SQLite WAL sessions: state, replay defense and restart persistence."""
import http.client
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import time
from urllib.parse import urlencode

binary=Path(sys.argv[1]).resolve()
project=Path(sys.argv[2]).resolve()
def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1",0))
        return sock.getsockname()[1]
with tempfile.TemporaryDirectory(prefix="noqeri-shared-sessions-") as tmp:
    folder=Path(tmp)
    sessionDb=folder/"sessions.sqlite"
    env=dict(os.environ,NOQERI_WEB_SESSION_SQLITE=str(sessionDb))
    ports=[free_port(),free_port()]
    processes=[]
    def start(port):
        process=subprocess.Popen([str(binary),"serve-app",str(project),
          str(port),str(folder/("state-"+str(port)+".nqkv"))],
          env=env,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        for _ in range(90):
            if process.poll() is not None:
                raise AssertionError("shared session server exited: "+process.stderr.read().decode())
            try:
                if request(port)[0]==200:return process
            except OSError: time.sleep(.05)
        process.terminate()
        raise AssertionError("shared session worker not available")
    def request(port,method="GET",cookie="",data=None):
        connection=http.client.HTTPConnection("127.0.0.1",port,timeout=5)
        headers={}
        if cookie:headers["Cookie"]=cookie
        if data is not None:headers["Content-Type"]="application/x-www-form-urlencoded"
        try:
            connection.request(method,"/",body=data,headers=headers)
            response=connection.getresponse()
            return response.status,dict(response.getheaders()),response.read().decode()
        finally:connection.close()
    def csrf(body):
        return re.search(r'name="_nq_csrf" value="([^"]+)"',body).group(1)
    def action(body):
        return re.search(r'name="_nq_action" value="([^"]+)"',body).group(1)
    try:
        processes=[start(port) for port in ports]
        _,headers,home=request(ports[0])
        cookie=headers["Set-Cookie"].split(";",1)[0]
        assert "Clicks: 0" in home
        assert "Clicks: 0" in request(ports[1],cookie=cookie)[2]
        payload=urlencode({"_nq_csrf":csrf(home),"_nq_action":action(home)})
        assert request(ports[0],"POST",cookie,payload)[0]==303
        assert "Clicks: 1" in request(ports[1],cookie=cookie)[2]
        assert request(ports[1],"POST",cookie,payload)[0]==403,"stale cross-worker CSRF succeeded"
        latest=request(ports[1],cookie=cookie)[2]
        payload2=urlencode({"_nq_csrf":csrf(latest),"_nq_action":action(latest)})
        assert request(ports[1],"POST",cookie,payload2)[0]==303
        assert "Clicks: 2" in request(ports[0],cookie=cookie)[2]
        processes[0].terminate()
        processes[0].communicate(timeout=5)
        processes[0]=start(ports[0])
        assert "Clicks: 2" in request(ports[0],cookie=cookie)[2], "restart lost shared session"
        assert sessionDb.is_file() and sessionDb.stat().st_size>0
    finally:
        for process in processes:
            if process.poll() is None:
                process.terminate()
                try:process.communicate(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill();process.communicate()
print("PASS: two process SQLite sessions, state, rotation, replay rejection and restart persistence")
