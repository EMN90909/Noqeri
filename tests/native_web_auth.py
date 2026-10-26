#!/usr/bin/env python3
"""Bounded native HTTP concurrency and browser-native auth regression."""
import base64
from concurrent.futures import ThreadPoolExecutor
import http.client
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

binary,source=sys.argv[1],str(Path(sys.argv[2]).resolve())
with tempfile.TemporaryDirectory() as td:
    with socket.socket() as probe:
        probe.bind(("127.0.0.1",0))
        port=probe.getsockname()[1]
    env=dict(os.environ)
    env.update(NOQERI_WEB_BASIC_USER="tester",NOQERI_WEB_BASIC_PASSWORD="long-secret-123",
               NOQERI_WEB_SECURE_COOKIE="1")
    server=subprocess.Popen([binary,"serve-app",source,str(port),str(Path(td)/"data.nqkv")],
        env=env,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    good="Basic "+base64.b64encode(b"tester:long-secret-123").decode()
    bad="Basic "+base64.b64encode(b"tester:wrong").decode()
    def request(auth=None,route="/"):
        client=http.client.HTTPConnection("127.0.0.1",port,timeout=5)
        headers={"Host":f"127.0.0.1:{port}"}
        if auth:headers["Authorization"]=auth
        client.request("GET",route,headers=headers)
        result=client.getresponse()
        answer=result.status,dict(result.getheaders()),result.read().decode()
        client.close()
        return answer
    try:
        for _ in range(90):
            if server.poll() is not None:
                raise AssertionError("auth server quit: "+server.stderr.read().decode())
            try:
                if request()[0]==401:break
            except OSError:time.sleep(.07)
        else:raise AssertionError("auth server not available")
        status,headers,_=request()
        assert status==401,(status,headers)
        assert "Basic realm" in headers.get("WWW-Authenticate",""),headers
        assert request(bad)[0]==401
        status,headers,body=request(good,route="/members/Ada")
        assert status==200 and "Hello, Ada" in body
        assert "HttpOnly" in headers["Set-Cookie"]
        assert "SameSite=Strict" in headers["Set-Cookie"]
        assert "Secure" in headers["Set-Cookie"]
        responses=[]
        with ThreadPoolExecutor(max_workers=16) as pool:
            responses=list(pool.map(lambda _:request(good),range(32)))
        assert all(status in (200,503) for status,_,_ in responses)
        assert any(status==200 for status,_,_ in responses)
    finally:
        server.terminate()
        try:server.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            server.kill();server.communicate()
print("PASS: optional Basic gate, secure cookies, bounded concurrent HTTP")
