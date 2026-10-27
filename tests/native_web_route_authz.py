#!/usr/bin/env python3
"""Native path-scoped authorization, method permissions and session identity isolation."""
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
source=Path(sys.argv[2]).resolve()
viewer="v"*40
writer="w"*40
with tempfile.TemporaryDirectory(prefix="noqeri-route-acl-") as td:
    root=Path(td)
    policy=root/"policy.txt"
    policy.write_text(
      "BOTH / NOQERI_TEST_WRITE_KEY\n"
      "GET / NOQERI_TEST_VIEW_KEY\n"
    )
    env=dict(os.environ,NOQERI_WEB_AUTHZ_FILE=str(policy),
             NOQERI_TEST_WRITE_KEY=writer,NOQERI_TEST_VIEW_KEY=viewer)
    with socket.socket() as sock:
        sock.bind(("127.0.0.1",0));port=sock.getsockname()[1]
    server=subprocess.Popen([str(binary),"serve-app",str(source),str(port),
       str(root/"state.nqkv")],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,env=env)
    def req(method="/",path="/",token="",cookie="",data=None):
        connection=http.client.HTTPConnection("127.0.0.1",port,timeout=4)
        headers={}
        if token:headers["Authorization"]="Bearer "+token
        if cookie:headers["Cookie"]=cookie
        if data is not None:headers["Content-Type"]="application/x-www-form-urlencoded"
        try:
            connection.request(method,path,body=data,headers=headers)
            response=connection.getresponse()
            return response.status,dict(response.getheaders()),response.read().decode()
        finally: connection.close()
    try:
        for _ in range(90):
            if server.poll() is not None:raise AssertionError(server.stderr.read().decode())
            try:
                if req("GET",token=writer)[0]==200:break
            except OSError:time.sleep(.05)
        else:raise AssertionError("ACL server startup failed")
        assert req("GET")[0]==403
        assert req("GET",token="bogus")[0]==403
        assert req("GET",token=viewer)[0]==200
        assert req("GET",token=writer)[0]==200

        _,writers,writerHtml=req("GET",token=writer)
        wCookie=writers["Set-Cookie"].split(";",1)[0]
        _,viewers,viewerHtml=req("GET",token=viewer)
        vCookie=viewers["Set-Cookie"].split(";",1)[0]
        assert wCookie!=vCookie
        assert "Your clicks: 0" in writerHtml and "Your clicks: 0" in viewerHtml
        csrf=re.search(r'name="_nq_csrf" value="([^"]+)"',writerHtml).group(1)
        action=re.search(r'name="_nq_action" value="([^"]+)"',writerHtml).group(1)
        payload=urlencode({"_nq_csrf":csrf,"_nq_action":action})
        assert req("POST",token=viewer,cookie=wCookie,data=payload)[0]==403
        assert req("POST",token=writer,cookie=vCookie,data=payload)[0]==403
        assert req("POST",token=writer,cookie=wCookie,data=payload)[0]==303
        assert req("POST",token=writer,cookie=wCookie,data=payload)[0]==403
        assert "Your clicks: 1" in req("GET",token=writer,cookie=wCookie)[2]
        assert "Your clicks: 0" in req("GET",token=viewer,cookie=vCookie)[2]

        # A differently authenticated identity must never inherit a prior SID.
        status,foreign,body=req("GET",token=viewer,cookie=wCookie)
        assert status==200 and "Your clicks: 0" in body
        assert foreign.get("Set-Cookie","").split(";",1)[0]!=wCookie
    finally:
        server.terminate()
        try:server.communicate(timeout=4)
        except subprocess.TimeoutExpired:server.kill();server.communicate()

    missing=dict(env);missing.pop("NOQERI_TEST_VIEW_KEY")
    rejected=subprocess.run([str(binary),"serve-app",str(source),"18478",
                             str(root/"other.nqkv")],capture_output=True,text=True,
                             env=missing,timeout=5)
    assert rejected.returncode!=0 and "missing route authorization token" in rejected.stderr,rejected.stderr
print("PASS: route-scoped bearer RBAC, POST deny, missing key fail-closed and identity-bound CSRF sessions")
