#!/usr/bin/env python3
"""HTTPS reverse-proxy origin boundary: simulated Host/Origin at loopback socket."""
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
with tempfile.TemporaryDirectory() as td:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1",0))
        port=sock.getsockname()[1]
    public="https://app.example.test"
    env={**os.environ,"NOQERI_WEB_PUBLIC_ORIGIN":public,
         "NOQERI_WEB_REQUIRE_ORIGIN":"1"}
    server=subprocess.Popen([str(binary),"serve-app",str(app),str(port),
                             str(Path(td)/"state.nqkv")],
                            env=env,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    def request(method="GET",body=None,host="app.example.test",origin=None,cookie=None):
        client=http.client.HTTPConnection("127.0.0.1",port,timeout=4)
        headers={"Host":host}
        if cookie:headers["Cookie"]=cookie
        if origin is not None:headers["Origin"]=origin
        if body is not None:headers["Content-Type"]="application/x-www-form-urlencoded"
        client.request(method,"/",body,headers)
        res=client.getresponse()
        output=res.status,dict(res.getheaders()),res.read().decode()
        client.close()
        return output
    try:
        for _ in range(70):
            if server.poll() is not None:raise AssertionError(server.stderr.read().decode())
            try:
                if request()[0]==200:break
            except OSError:time.sleep(.07)
        else:raise AssertionError("native HTTPS proxy backend failed to start")
        assert request(host="evil.example.test")[0]==400
        status,headers,body=request()
        assert status==200
        assert "Secure" in headers.get("Set-Cookie","")
        assert "HttpOnly" in headers["Set-Cookie"]
        cookie=headers["Set-Cookie"].split(";",1)[0]
        csrf=re.search(r'name="_nq_csrf" value="([a-z0-9]+)"',body).group(1)
        action=re.search(r'name="_nq_action" value="(\d+)"',body).group(1)
        payload=urllib.parse.urlencode({"_nq_csrf":csrf,"_nq_action":action})
        assert request("POST",payload,cookie=cookie)[0]==400
        assert request("POST",payload,origin="http://app.example.test",cookie=cookie)[0]==403
        assert request("POST",payload,origin="https://evil.example.test",cookie=cookie)[0]==403
        assert request("POST",payload,origin=public,cookie=cookie)[0]==303
    finally:
        server.terminate()
        try:server.communicate(timeout=4)
        except subprocess.TimeoutExpired:
            server.kill();server.communicate()
print("PASS: explicit TLS-proxy authority, HTTPS POST Origin, Secure session cookies")
