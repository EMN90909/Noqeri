#!/usr/bin/env python3
"""Compile a standalone native app: no runtime .noqeri, npm, Node or JS."""
import http.client
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

builder=Path(sys.argv[1]).resolve()
project=Path(sys.argv[2]).resolve()

with tempfile.TemporaryDirectory() as td:
    root=Path(td)
    app=root/"native-app"
    cp=subprocess.run([str(builder),"compile",str(project),str(app)],
        text=True,capture_output=True,timeout=95)
    assert cp.returncode==0,(cp.stdout,cp.stderr)
    assert app.is_file() and os.access(app,os.X_OK)
    assert not list(root.glob("*.cpp")), "generator intermediate leaked"
    # The binary must operate after the original source has become inaccessible.
    copy=root/"isolated"
    copy.mkdir()
    with socket.socket() as sock:
        sock.bind(("127.0.0.1",0))
        port=sock.getsockname()[1]
    proc=subprocess.Popen([str(app),str(port),str(copy/"state.nqkv")],
        cwd=copy,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        for _ in range(80):
            if proc.poll() is not None:
                raise AssertionError("standalone app crashed: "+proc.stderr.read().decode())
            try:
                client=http.client.HTTPConnection("127.0.0.1",port,timeout=2)
                client.request("GET","/members/Ada")
                response=client.getresponse()
                html=response.read().decode()
                if response.status==200:
                    assert "Hello, Ada" in html
                    assert "Noqeri independent full-stack preview" in html
                    assert "<script" not in html
                    client.close()
                    break
            except OSError:
                time.sleep(.08)
        else:
            raise AssertionError("standalone app did not serve")
    finally:
        proc.terminate()
        try:proc.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill();proc.communicate()
print("PASS: compiled standalone native Noqeri app, independent from source files and JavaScript")
