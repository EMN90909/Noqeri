#!/usr/bin/env python3
"""No Node/JS test harness for the native server. Uses Python stdlib only in CI."""
import http.client
import os
import pathlib
import re
import socket
import subprocess
import sys
import tempfile
import time
import urllib.parse

binary, source = sys.argv[1:]
source = pathlib.Path(source).resolve()

def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]

def request(port, method, path, data=None, cookie=None, content_type="application/x-www-form-urlencoded"):
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=4)
    headers = {"Host": f"127.0.0.1:{port}"}
    if cookie:
        headers["Cookie"] = cookie
    if data is not None:
        headers["Content-Type"] = content_type
    conn.request(method, path, body=data, headers=headers)
    res = conn.getresponse()
    body = res.read().decode("utf-8")
    result = res.status, dict(res.getheaders()), body
    conn.close()
    return result

def start(port, file):
    process = subprocess.Popen([binary, "serve-app", str(source), str(port), str(file)],
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    for _ in range(80):
        if process.poll() is not None:
            raise AssertionError("server exited: " + process.stderr.read().decode())
        try:
            status, _, _ = request(port, "GET", "/theme.css")
            if status == 200:
                return process
        except OSError:
            pass
        time.sleep(.08)
    process.terminate()
    raise AssertionError("native server startup timed out")

def stop(process):
    process.terminate()
    try:
        process.communicate(timeout=4)
    except subprocess.TimeoutExpired:
        process.kill()
        process.communicate(timeout=2)

def csrf_of(body):
    match = re.search(r'name="_nq_csrf" value="([a-z0-9]+)"', body)
    assert match, "missing CSRF token"
    return match.group(1)

def action_of(body, index=0):
    ids = re.findall(r'name="_nq_action" value="(\d+)"', body)
    assert len(ids) >= 2, ids
    return ids[index]

with tempfile.TemporaryDirectory() as temp:
    state_file = pathlib.Path(temp) / "storage" / "records.nqkv"
    port = free_port()
    server = start(port, state_file)
    try:
        status, headers, home = request(port, "GET", "/")
        assert status == 200 and "Your clicks: 0" in home
        assert "All-time clicks: 0" in home
        assert "<script" not in home.lower()
        assert "javascript:" not in home.lower()
        cookie = headers["Set-Cookie"].split(";", 1)[0]
        assert "HttpOnly" in headers["Set-Cookie"]
        assert "SameSite=Strict" in headers["Set-Cookie"]
        assert "Content-Security-Policy" in headers
        csrf, increment = csrf_of(home), action_of(home)
        payload = urllib.parse.urlencode({"_nq_csrf": csrf, "_nq_action": increment})
        assert request(port, "POST", "/", payload, cookie)[0] == 303
        assert request(port, "POST", "/", payload, cookie)[0] == 403, "CSRF token replay worked"
        status, _, updated = request(port, "GET", "/", cookie=cookie)
        assert status == 200 and "Your clicks: 1" in updated and "All-time clicks: 1" in updated
        status, headers2, other = request(port, "GET", "/")
        cookie2 = headers2["Set-Cookie"].split(";", 1)[0]
        assert cookie != cookie2 and "Your clicks: 0" in other and "All-time clicks: 1" in other
        assert request(port, "POST", "/", payload, cookie2)[0] == 403
        assert request(port, "POST", "/", payload)[0] == 403

        payload = urllib.parse.urlencode({
            "_nq_csrf": csrf_of(updated), "_nq_action": action_of(updated, 1),
            "displayName": "<img src=x onerror=alert(1)>& name"})
        assert request(port, "POST", "/", payload, cookie)[0] == 303
        status, _, form_result = request(port, "GET", "/", cookie=cookie)
        assert "Hello, &lt;img src=x onerror=alert(1)&gt;&amp; name" in form_result
        assert "Hello, <img" not in form_result
        assert request(port, "GET", "/about")[0] == 200
        assert request(port, "GET", "/missing")[0] == 404
        assert request(port, "GET", "/theme.css")[0] == 200
        assert request(port, "POST", "/", "x=1", cookie)[0] == 403
        assert request(port, "POST", "/", "x=1", cookie, "text/plain")[0] == 400
        assert request(port, "GET", "/../secrets")[0] == 400
        # Raw malformed requests: reject ambiguous framing, spoofed Host and
        # session-cookie ambiguity before handling any mutation.
        def raw_request(payload):
            with socket.create_connection(("127.0.0.1",port), timeout=3) as sock:
                sock.sendall(payload)
                return sock.recv(4096).split(b"\r\n", 1)[0]
        assert b"400" in raw_request(b"GET / HTTP/1.1\r\n\r\n")
        assert b"400" in raw_request(
            b"GET / HTTP/1.1\r\nHost: attacker.test\r\n\r\n")
        assert b"400" in raw_request(
            f"GET / HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nHost: localhost:{port}\r\n\r\n".encode())
        assert b"400" in raw_request(
            f"POST / HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n"
            "Content-Length: 3\r\nContent-Length: 3\r\n"
            "Content-Type: application/x-www-form-urlencoded\r\n\r\nx=1".encode())
        assert b"400" in raw_request(
            f"GET / HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n"
            "Transfer-Encoding: chunked\r\n\r\n".encode())
        assert b"400" in raw_request(
            f"GET / HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n"
            "X-Control: bad\x01value\r\n\r\n".encode())
        # An ambiguous session cookie must never authenticate a POST.
        assert request(port,"POST","/",payload,
            f"{cookie}; {cookie}")[0] == 403

        assert state_file.exists() and state_file.read_text().startswith("NQKV1\n")
    finally:
        stop(server)
    server = start(port, state_file)
    try:
        status, _, home = request(port, "GET", "/")
        assert status == 200 and "All-time clicks: 1" in home and "Your clicks: 0" in home
    finally:
        stop(server)

print("PASS: no-JS buttons, native forms, per-session state, rotated CSRF, escaped fields, durable storage, restart")
