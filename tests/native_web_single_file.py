#!/usr/bin/env python3
"""End-to-end Noqeri one-file web: CSS, SSR, backend form, CSRF and HTML escaping."""
import http.client
import pathlib
import re
import socket
import subprocess
import sys
import tempfile
import time
import urllib.parse

binary = sys.argv[1]
source = pathlib.Path(sys.argv[2]).resolve()
assert source.is_file() and source.suffix == ".noqeri"

with tempfile.TemporaryDirectory() as folder:
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    process = subprocess.Popen(
        [binary, "serve-app", str(source), str(port),
         str(pathlib.Path(folder) / "state.nqkv")],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
    )

    def request(method, path, payload=None, cookie=None):
        conn = http.client.HTTPConnection("127.0.0.1", port, timeout=4)
        headers = {}
        if payload is not None:
            headers["Content-Type"] = "application/x-www-form-urlencoded"
        if cookie:
            headers["Cookie"] = cookie
        conn.request(method, path, body=payload, headers=headers)
        response = conn.getresponse()
        result = response.status, dict(response.getheaders()), response.read().decode()
        conn.close()
        return result

    try:
        for _ in range(80):
            if process.poll() is not None:
                raise AssertionError("server startup failed: " + process.stderr.read().decode())
            try:
                if request("GET", "/theme.css")[0] == 200:
                    break
            except OSError:
                time.sleep(0.05)
        else:
            raise AssertionError("single-file app did not start")

        status, _, css = request("GET", "/theme.css")
        assert status == 200
        assert ".hero {" in css and "@media (max-width: 600px)" in css
        assert "--nq-primary:#fb5607" in css

        status, headers, home = request("GET", "/")
        assert status == 200
        assert "Hello from Noqeri" in home and "Messages received: 0" in home
        assert '<script' not in home.lower()
        assert 'class="hero"' in home

        cookie = headers["Set-Cookie"].split(";", 1)[0]
        csrf = re.search(r'name="_nq_csrf" value="([^"]+)"', home).group(1)
        action = re.search(r'name="_nq_action" value="([^"]+)"', home).group(1)

        def payload(message):
            return urllib.parse.urlencode({
                "_nq_action": action, "_nq_csrf": csrf, "message": message,
            })

        assert request("POST", "/", payload("x"), cookie)[0] == 422
        assert request("POST", "/", payload("<b>Hello!</b>"), cookie)[0] == 303
        updated = request("GET", "/", cookie=cookie)[2]
        assert "&lt;b&gt;Hello!&lt;/b&gt;" in updated
        assert "Messages received: 1" in updated
        assert request("POST", "/", payload("replay"), cookie)[0] == 403
        assert request("GET", "/theme.css")[2] == css
    finally:
        process.terminate()
        try:
            process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()

print("PASS: one-file CSS, SSR, native backend, CSRF and HTML escaping")
