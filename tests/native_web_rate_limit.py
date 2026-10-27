#!/usr/bin/env python3
"""Verify opt-in per-principal request quotas and fail-closed configuration."""
import http.client
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

binary=str(Path(sys.argv[1]).resolve())
source=str(Path(sys.argv[2]).resolve())
viewer="v"*40
writer="w"*40
with tempfile.TemporaryDirectory(prefix="noqeri-native-rate-") as td:
    policy=Path(td)/"policy.txt"
    policy.write_text("BOTH / NOQERI_TEST_WRITER\nBOTH / NOQERI_TEST_VIEWER\n")
    env=dict(os.environ,NOQERI_WEB_AUTHZ_FILE=str(policy),NOQERI_TEST_WRITER=writer,
             NOQERI_TEST_VIEWER=viewer,NOQERI_WEB_RATE_LIMIT_PER_MINUTE="3")
    with socket.socket() as s:
        s.bind(("127.0.0.1",0));port=s.getsockname()[1]
    server=subprocess.Popen([binary,"serve-app",source,str(port),str(Path(td)/"state.nqkv")],
                            env=env,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    def request(token):
        conn=http.client.HTTPConnection("127.0.0.1",port,timeout=5)
        try:
            conn.request("GET","/",headers={"Authorization":"Bearer "+token})
            response=conn.getresponse()
            result=response.status,response.read()
            return result
        finally:conn.close()
    try:
        for _ in range(80):
            if server.poll() is not None:raise AssertionError(server.stderr.read().decode())
            try:
                if request(writer)[0]==200:break
            except OSError:time.sleep(.06)
        else:raise AssertionError("server failed to start")
        assert [request(writer)[0] for _ in range(3)]==[200,200,429]
        assert [request(viewer)[0] for _ in range(4)]==[200,200,200,429]
        assert request("invalid")[0]==403
    finally:
        server.terminate()
        try:server.communicate(timeout=5)
        except subprocess.TimeoutExpired:server.kill();server.communicate()
    for invalid in ("0","10001","2.5","a","-1"):
        failed=subprocess.run([binary,"serve-app",source,"18188",str(Path(td)/"bad.nqkv")],
                              env=dict(env,NOQERI_WEB_RATE_LIMIT_PER_MINUTE=invalid),
                              text=True,capture_output=True,timeout=5)
        assert failed.returncode!=0 and "rate limit" in failed.stderr,(invalid,failed.stderr)
print("PASS: per-principal request quotas, identity isolation, 429 and invalid-config fail-closed")
