#!/usr/bin/env python3
"""Security: enforce strict HTTP framing and a total slow-client read deadline."""
import http.client
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

binary=Path(sys.argv[1]).resolve()
source=Path(sys.argv[2]).resolve()
with tempfile.TemporaryDirectory(prefix="noqeri-request-deadline-") as td:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1",0));port=sock.getsockname()[1]
    server=subprocess.Popen([str(binary),"serve-app",str(source),str(port),
        str(Path(td)/"state.nqkv")],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    def raw(payload):
        with socket.create_connection(("127.0.0.1",port),timeout=6) as sock:
            sock.settimeout(6)
            sock.sendall(payload)
            data=sock.recv(4096)
            return data.split(b"\r\n",1)[0]
    try:
        for _ in range(80):
            if server.poll() is not None:raise AssertionError(server.stderr.read().decode())
            try:
                if b"200" in raw(f"GET /theme.css HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n\r\n".encode()):
                    break
            except OSError:time.sleep(.06)
        else:raise AssertionError("server did not start")

        prefix=f"GET / HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n"
        assert b"400" in raw((prefix+"X-Check: one\nmalformed\r\n\r\n").encode()),"bare LF accepted"
        assert b"400" in raw((prefix+"X-Check: one\rmalformed\r\n\r\n").encode()),"bare CR accepted"
        assert b"400" in raw((prefix+("X-Bounded: header\r\n"*65)+"\r\n").encode()),"header count unbounded"
        assert b"400" in raw((prefix+"Content-Length: 0\r\nTransfer-Encoding: chunked\r\n\r\n").encode()),"TE/CL accepted"

        # Drip bytes at 3.5-second intervals: a per-read timeout alone would
        # permit a client to hold a worker indefinitely. The 4-second total
        # deadline must terminate this incomplete request after its first drip.
        with socket.create_connection(("127.0.0.1",port),timeout=7) as sock:
            sock.settimeout(7)
            start=time.monotonic()
            sock.sendall(b"GET / HTTP/1.1\r\nHost: 127.")
            time.sleep(3.5)
            sock.sendall(b"0")
            line=sock.recv(4096).split(b"\r\n",1)[0]
            elapsed=time.monotonic()-start
            assert b"400" in line,(line,elapsed)
            assert elapsed < 6.0,elapsed

        assert b"200" in raw((prefix+"\r\n").encode()),"server stopped after malformed requests"
    finally:
        server.terminate()
        try:server.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill();server.communicate()
print("PASS: bounded header count, strict CRLF, TE/CL reject and total slow-client timeout")
