#!/usr/bin/env python3
"""Native session-expiry and explicit strict Origin regression (no browser JS)."""
import http.client
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import time
import urllib.parse

binary=Path(sys.argv[1]).resolve()
app=Path(sys.argv[2]).resolve()
with tempfile.TemporaryDirectory() as directory:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1",0))
        port=sock.getsockname()[1]
    env={**os.environ,"NOQERI_WEB_REQUIRE_ORIGIN":"1",
         "NOQERI_WEB_SESSION_TTL_SECONDS":"1"}
    process=subprocess.Popen([str(binary),"serve-app",str(app),str(port),
                              str(Path(directory)/"state.nqkv")],
                             env=env,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    def request(method="GET",data=None,cookie=None,origin=None):
        conn=http.client.HTTPConnection("127.0.0.1",port,timeout=4)
        headers={"Host":f"127.0.0.1:{port}"}
        if cookie:headers["Cookie"]=cookie
        if data is not None:headers["Content-Type"]="application/x-www-form-urlencoded"
        if origin is not None:headers["Origin"]=origin
        conn.request(method,"/",body=data,headers=headers)
        response=conn.getresponse()
        result=response.status,dict(response.getheaders()),response.read().decode()
        conn.close()
        return result
    try:
        for _ in range(70):
            if process.poll() is not None:
                raise AssertionError(process.stderr.read().decode())
            try:
                if request()[0]==200:break
            except OSError:time.sleep(.07)
        else:raise AssertionError("native app did not start")
        status,headers,body=request()
        assert status==200
        cookie=headers["Set-Cookie"].split(";",1)[0]
        csrf=re.search(r'name="_nq_csrf" value="([a-z0-9]+)"',body).group(1)
        action=re.search(r'name="_nq_action" value="(\d+)"',body).group(1)
        payload=urllib.parse.urlencode({"_nq_csrf":csrf,"_nq_action":action})
        assert request("POST",payload,cookie)[0]==400,"strict Origin missing accepted"
        assert request("POST",payload,cookie,"http://attacker.test")[0]==403
        assert request("POST",payload,cookie,f"http://127.0.0.1:{port}")[0]==303
        # Reuse a rotated token is forbidden, even inside the one-second TTL.
        assert request("POST",payload,cookie,f"http://127.0.0.1:{port}")[0]==403
        status,headers,body=request(cookie=cookie)
        assert status==200
        fresh_csrf=re.search(r'name="_nq_csrf" value="([a-z0-9]+)"',body).group(1)
        payload=urllib.parse.urlencode({"_nq_csrf":fresh_csrf,"_nq_action":action})
        time.sleep(1.15)
        assert request("POST",payload,cookie,f"http://127.0.0.1:{port}")[0]==403, "expired session POST accepted"
        status,headers,_=request(cookie=cookie)
        assert status==200
        assert headers.get("Set-Cookie"),"expired GET should issue new session"
        assert headers["Set-Cookie"].split(";",1)[0]!=cookie
    finally:
        process.terminate()
        try:process.communicate(timeout=4)
        except subprocess.TimeoutExpired:
            process.kill();process.communicate()
print("PASS: strict Origin opt-in, expiry, replay rejection and fresh sessions")
