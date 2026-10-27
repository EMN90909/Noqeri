#!/usr/bin/env python3
"""Prove the native static and SSR toolchain runs with no Node executable on PATH."""
import http.client
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

compiler=Path(sys.argv[1]).resolve()
native=Path(sys.argv[2]).resolve()
static=Path(sys.argv[3]).resolve()
actions=Path(sys.argv[4]).resolve()
environment=dict(os.environ)
environment["PATH"]="/noqeri-no-node-or-external-tools"
environment["NOQERI_WEB_BIN"]=str(native)
with tempfile.TemporaryDirectory(prefix="noqeri-node-free-") as folder:
    root=Path(folder)
    result=subprocess.run([str(compiler),"web","build",str(static),str(root/"site")],
      capture_output=True,text=True,env=environment,timeout=30)
    assert result.returncode==0,(result.stdout,result.stderr)
    assert (root/"site"/"index.html").is_file()
    assert not list((root/"site").rglob("*.js"))
    assert not list((root/"site").rglob("*.mjs"))
    with socket.socket() as sock:
        sock.bind(("127.0.0.1",0))
        port=sock.getsockname()[1]
    environment["NOQERI_WEB_REACTIVE"]="1"
    server=subprocess.Popen([str(compiler),"web","serve-app",str(actions),str(port),
      str(root/"state.nqkv")],env=environment,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        for _ in range(80):
            if server.poll() is not None:
                raise AssertionError(server.stderr.read().decode())
            try:
                connection=http.client.HTTPConnection("127.0.0.1",port,timeout=2)
                connection.request("GET","/")
                response=connection.getresponse()
                data=response.read()
                connection.close()
                if response.status==200:
                    assert b"Noqeri native interactive web" in data
                    break
            except OSError:
                time.sleep(.05)
        else:
            raise AssertionError("native web server did not start")
    finally:
        server.terminate()
        try:
            server.communicate(timeout=4)
        except subprocess.TimeoutExpired:
            server.kill()
            server.communicate()
print("PASS: Noqeri static build and interactive SSR start with Node.js absent from PATH")
