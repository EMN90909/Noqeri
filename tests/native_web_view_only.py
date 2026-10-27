#!/usr/bin/env python3
"""Node-free compiler regression: one standalone view can be static or SSR."""
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
with tempfile.TemporaryDirectory(prefix="noqeri-standalone-view-") as td:
    root=Path(td)
    static=root/"static";static.mkdir()
    (static/"main.noqeri").write_text('let name = "Noqeri"\nview { main { h1 { "Standalone view" } p { "Hello " + name } } }\n')
    no_node=dict(os.environ,PATH="/noqeri-without-node")
    build=subprocess.run([str(binary),"build",str(static),str(root/"dist")],
                         env=no_node,capture_output=True,text=True,timeout=25)
    assert build.returncode==0,(build.stdout,build.stderr)
    html=(root/"dist"/"index.html").read_text()
    assert "Standalone view" in html and "Hello Noqeri" in html
    assert not list((root/"dist").rglob("*.js"))

    source=root/"interactive";source.mkdir()
    (source/"main.noqeri").write_text(
      'let clicks = 0\nview { main { h1 { "Standalone interactive view" } '
      'p { "Count: " + clicks } button on.click { clicks = clicks + 1 } { "Increase" } } }\n')
    rejected=subprocess.run([str(binary),"build",str(source),str(root/"invalid")],
                            env=no_node,capture_output=True,text=True,timeout=25)
    assert rejected.returncode!=0 and "events require serve-app" in rejected.stderr

    with socket.socket() as sock:
        sock.bind(("127.0.0.1",0));port=sock.getsockname()[1]
    env=dict(no_node,NOQERI_WEB_REACTIVE="1")
    server=subprocess.Popen([str(binary),"serve-app",str(source),str(port),str(root/"state.nqkv")],
                            env=env,stderr=subprocess.PIPE,stdout=subprocess.DEVNULL)
    def request(method,path,body=None,cookie=""):
        connection=http.client.HTTPConnection("127.0.0.1",port,timeout=5)
        headers={}
        if cookie:headers["Cookie"]=cookie
        if body is not None:headers["Content-Type"]="application/x-www-form-urlencoded"
        try:
            connection.request(method,path,body=body,headers=headers)
            response=connection.getresponse()
            return response.status,dict(response.getheaders()),response.read().decode()
        finally:connection.close()
    try:
        for _ in range(80):
            if server.poll() is not None:raise AssertionError(server.stderr.read().decode())
            try:
                result=request("GET","/")
                if result[0]==200:break
            except OSError:time.sleep(.06)
        else:raise AssertionError("view-only SSR app did not start")
        status,headers,body=result
        assert status==200 and "Count: 0" in body and "/__nq/reactive.js" in body
        csrf=re.search(r'name="_nq_csrf" value="([^"]+)"',body).group(1)
        action=re.search(r'name="_nq_action" value="([^"]+)"',body).group(1)
        cookie=headers["Set-Cookie"].split(";",1)[0]
        payload=urlencode({"_nq_csrf":csrf,"_nq_action":action})
        assert request("POST","/",payload,cookie)[0]==303
        assert "Count: 1" in request("GET","/",cookie=cookie)[2]
        assert request("POST","/",payload,cookie)[0]==403
        assert request("GET","/missing")[0]==404
    finally:
        server.terminate()
        try:server.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill();server.communicate()
print("PASS: standalone Noqeri view builds without Node; reactive server actions stay session/CSRF protected")
