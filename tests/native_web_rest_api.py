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

root = pathlib.Path(__file__).resolve().parent.parent
binary = sys.argv[1] if len(sys.argv) > 1 else str(root / "build" / "noqeri-web")
source = sys.argv[2] if len(sys.argv) > 2 else str(root / "examples" / "native_web_rest_api.noqeri")
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
    env = dict(os.environ, MY_API_SECRET="secret_token_123")
    proc = subprocess.Popen(binary_cmd + [str(port)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=env)
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

        # 1. Test GET /hello
        status, headers, body = request(port, "GET", "/hello")
        assert status == 200, f"Expected 200, got {status}"
        assert "application/json" in headers.get("content-type", "").lower(), f"Expected application/json, got {headers.get('content-type')}"
        parsed = json.loads(body)
        assert parsed.get("message") == "Hello from Noqeri!", f"Unexpected body: {body}"

        # 2. Test GET /users/:id param extraction with leading zero preservation
        status, headers, body = request(port, "GET", "/users/0012")
        assert status == 200, f"Expected 200, got {status}"
        assert "application/json" in headers.get("content-type", "").lower()
        parsed = json.loads(body)
        assert parsed.get("id") == "0012", f"Expected id '0012', got {parsed.get('id')}"
        assert isinstance(parsed.get("id"), str), f"Expected string type for id, got {type(parsed.get('id'))}"

        # 3. Test 405 Method Not Allowed + Allow header
        status, headers, body = request(port, "DELETE", "/hello")
        assert status == 405, f"Expected 405 Method Not Allowed, got {status}"
        assert "allow" in headers, f"Expected Allow header, got {headers}"
        assert "GET" in headers["allow"], f"Expected GET in Allow header: {headers['allow']}"

        # 4. Test POST /users with JSON body
        payload = json.dumps({"name": "Alice"})
        status, headers, body = request(port, "POST", "/users", payload=payload, headers={"Content-Type": "application/json"})
        assert status == 200, f"Expected 200, got {status}"
        parsed = json.loads(body)
        assert parsed.get("user") == "Alice", f"Expected user Alice, got {parsed}"
        assert parsed.get("created") is True, f"Expected created True, got {parsed}"

        # 5. Test POST /users with invalid JSON body -> 400 Bad Request
        status, headers, body = request(port, "POST", "/users", payload="{'invalid': json}", headers={"Content-Type": "application/json"})
        assert status == 400, f"Expected 400 Bad Request for invalid JSON, got {status}"
        status, headers, body = request(port, "POST", "/users", payload='{"name": [not json]}', headers={"Content-Type": "application/json"})
        assert status == 400, f"Expected 400 Bad Request for malformed nested JSON, got {status}"

        # 6. Test route-specific authorization
        # 6a. Missing auth token -> 401
        status, _, _ = request(port, "GET", "/secret")
        assert status == 401, f"Expected 401 Unauthorized, got {status}"

        # 6b. Wrong auth token -> 401
        status, _, _ = request(port, "GET", "/secret", headers={"Authorization": "Bearer wrong_secret"})
        assert status == 401, f"Expected 401 Unauthorized, got {status}"

        # 6c. Missing strict Bearer prefix -> 401
        status, _, _ = request(port, "GET", "/secret", headers={"Authorization": "secret_token_123"})
        assert status == 401, f"Expected 401 Unauthorized for missing Bearer prefix, got {status}"

        # 6d. Correct auth token with Bearer prefix -> 200
        status, _, body = request(port, "GET", "/secret", headers={"Authorization": "Bearer secret_token_123"})
        assert status == 200, f"Expected 200 OK, got {status}"
        parsed = json.loads(body)
        assert parsed.get("secret") == "super-confidential", f"Unexpected secret response: {body}"

        # 7. Test typed JSON AST (numbers, floats, booleans, null, arrays, nested objects, unicode escapes)
        status, _, body = request(port, "GET", "/types")
        assert status == 200, f"Expected 200 OK, got {status}"
        parsed = json.loads(body)
        assert parsed.get("str") == "0012" and isinstance(parsed.get("str"), str), f"str mismatch: {parsed.get('str')}"
        assert parsed.get("num") == 42 and isinstance(parsed.get("num"), int), f"num mismatch: {parsed.get('num')}"
        assert abs(parsed.get("neg") - (-15.5)) < 1e-6, f"neg mismatch: {parsed.get('neg')}"
        assert parsed.get("flag") is True, f"flag mismatch: {parsed.get('flag')}"
        assert parsed.get("none") is None, f"none mismatch: {parsed.get('none')}"
        assert parsed.get("arr") == ["item1", "item2"], f"arr mismatch: {parsed.get('arr')}"
        assert parsed.get("nested", {}).get("inner") == "val", f"nested mismatch: {parsed.get('nested')}"
        assert parsed.get("unicode") == "testA", f"unicode mismatch: {parsed.get('unicode')}"

        # 8. Test body size limit (deterministic 413 without connection reset / try-except)
        large_payload = "x" * 6000
        status, _, _ = request(port, "POST", "/hello", payload=large_payload, headers={"Content-Type": "application/json"})
        assert status == 413, f"Expected 413 Payload Too Large, got {status}"

        print("All REST API server checks passed.")
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

print("PASS: native REST API, method routing, auth, json AST, and deterministic limits")
