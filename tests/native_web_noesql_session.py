#!/usr/bin/env python3
"""NoeSQL v2 ABI adapter contract test with a native mock (not NoeSQL storage)."""
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
repo=Path(sys.argv[2]).resolve()
with tempfile.TemporaryDirectory() as td:
    root=Path(td)
    lib=root/"libnoesql-v2-mock.so"
    compile=subprocess.run(["c++","-std=c++17","-fPIC","-shared",
        str(repo/"tests/cpp/noesql_v2_mock.cpp"),"-o",str(lib)],
        capture_output=True,text=True,timeout=40)
    assert compile.returncode==0,(compile.stdout,compile.stderr)
    app=root/"app";app.mkdir()
    (app/"main.noqeri").write_text('''
database { engine: "noesql" path: "test.noedb" }
query people = "users get name"
route "/" {
    main {
        p { people }
        form on.submit {
            validate(name, 1, 64)
            db.execute("users add { name: $name }", name: form.name)
        } {
            input name: "name" type: "text" {}
            button type: "submit" { "Save" }
        }
    }
}
''')
    with socket.socket() as probe:
        probe.bind(("127.0.0.1",0));port=probe.getsockname()[1]
    env=dict(os.environ)
    env.pop("NOESQL_BIN",None)
    env["NOESQL_NATIVE_PROVIDER"]=str(lib)
    env["NOESQL_V2_MOCK_LOG"]=str(root/"callbacks.log")
    server=subprocess.Popen([str(binary),"serve-app",str(app),str(port),
        str(root/"session.nqkv")],env=env,stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE)
    def request(method,body=None,cookie=None):
        conn=http.client.HTTPConnection("127.0.0.1",port,timeout=4)
        headers={}
        if body is not None:headers["Content-Type"]="application/x-www-form-urlencoded"
        if cookie:headers["Cookie"]=cookie
        conn.request(method,"/",body,headers)
        response=conn.getresponse()
        result=response.status,dict(response.getheaders()),response.read().decode()
        conn.close()
        return result
    try:
        for _ in range(70):
            if server.poll() is not None:
                raise AssertionError(server.stderr.read().decode())
            try:
                if request("GET")[0]==200:break
            except OSError:time.sleep(.07)
        else:raise AssertionError("NoeSQL native ABI app did not start")
        status,headers,home=request("GET")
        assert status==200 and "native-v2-callback" in home
        cookie=headers["Set-Cookie"].split(";",1)[0]
        token=re.search(r'name="_nq_csrf" value="([^"]+)"',home).group(1)
        action=re.search(r'name="_nq_action" value="([^"]+)"',home).group(1)
        payload=urllib.parse.urlencode({
            "_nq_action":action,"_nq_csrf":token,"name":"A & B <script>"})
        assert request("POST",payload,cookie)[0]==303
        callback_log=(root/"callbacks.log").read_text()
        assert "users get name" in callback_log
        assert "users add { name: $name }" in callback_log
        assert "A & B <script>" in callback_log
        assert "name:text" not in callback_log
    finally:
        server.terminate()
        try:server.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            server.kill();server.communicate()
print("PASS: native NoeSQL v2 callback ABI, typed provider boundary, transaction calls and bound arguments")
