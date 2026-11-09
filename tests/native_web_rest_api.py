#!/usr/bin/env python3
"""End-to-end test for Noqeri native REST API and security features."""
import http.client
import json
import os
import pathlib
import socket
import subprocess
import sys
import tempfile
import time

binary = sys.argv[1] if len(sys.argv) > 1 else "./build/noqeri-web"
source = sys.argv[2] if len(sys.argv) > 2 else "examples/native_web_rest_api.noqeri"
source = pathlib.Path(source).resolve()
assert source.is_file() and source.suffix == ".noqeri"

def get_free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]

def request(port, method, path, payload=None, headers=None):
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    req_headers = headers or {}
    conn.request(method, path, body=payload, headers=req_headers)
    response = conn.getresponse()
    headers = {k.lower(): v for k, v in response.getheaders()}
    result = response.status, headers, response.read().decode()
    conn.close()
    return result

def test_rest_api_server(binary_cmd):
    port = get_free_port()
    proc = subprocess.Popen(binary_cmd + [str(port)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        # Wait for server to bind
        ready = False
        for _ in range(50):
            try:
                status, headers, body = request(port, "GET", "/hello")
                ready = True
                break
            except Exception:
                time.sleep(0.05)
        assert ready, "Server did not start up in time"

        # 1. Test /hello
        status, headers, body = request(port, "GET", "/hello")
        assert status == 200, f"Expected 200, got {status}"
        assert "application/json" in headers.get("content-type", "").lower(), f"Expected application/json, got {headers.get('content-type')}"
        parsed = json.loads(body)
        assert parsed.get("message") == "Hello from Noqeri!", f"Unexpected body: {body}"

        # 2. Test /users/:id param extraction
        status, headers, body = request(port, "GET", "/users/1042")
        assert status == 200, f"Expected 200, got {status}"
        assert "application/json" in headers.get("content-type", "").lower()
        parsed = json.loads(body)
        assert str(parsed.get("id")) == "1042", f"Expected id 1042, got {parsed}"

        # 3. Test body size limit (security: default limit 4096 bytes rejects overly large payload)
        large_payload = "x" * 6000
        try:
            status, _, _ = request(port, "POST", "/hello", payload=large_payload, headers={"Content-Type": "application/json"})
            assert status == 413, f"Expected 413 Payload Too Large, got {status}"
        except (http.client.RemoteDisconnected, ConnectionResetError):
            pass # Connection dropped due to overflow is also safe

        print("REST API server checks passed.")
    finally:
        proc.terminate()
        proc.wait(timeout=3)

# Test 1: serve-app mode
print("Testing serve-app mode...")
with tempfile.TemporaryDirectory() as folder:
    state_file = pathlib.Path(folder) / "state.nqkv"
    test_rest_api_server([binary, "serve-app", str(source)])

# Test 2: compile mode
print("Testing compile to standalone native executable...")
with tempfile.TemporaryDirectory() as folder:
    out_bin = pathlib.Path(folder) / "rest_api_bin"
    comp = subprocess.run([binary, "compile", str(source), str(out_bin)], capture_output=True, text=True)
    assert comp.returncode == 0, f"Compilation failed: {comp.stderr}"
    assert out_bin.is_file() and os.access(out_bin, os.X_OK)

    test_rest_api_server([str(out_bin)])

print("PASS: native REST API, json responses, param binding, and security limits")
