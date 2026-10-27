#!/usr/bin/env python3
"""Hermetic native HTTPS provider contract: actual libcurl over verified loopback TLS."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from tempfile import TemporaryDirectory
from threading import Thread
import http.client
import json
import os
import re
import socket
import ssl
import subprocess
import sys
import time
import urllib.parse

binary = str(Path(sys.argv[1]).resolve())

def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]

class Provider(BaseHTTPRequestHandler):
    requests = []
    def log_message(self, *_):
        pass
    def respond(self, data):
        raw = json.dumps(data).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)
    def receive(self, method):
        n = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(n) if n else b""
        self.requests.append((method, self.path, dict(self.headers), body))
        if "/documents/" in self.path:
            self.respond({"documents": [{"name": "docs/notes/one", "fields": {"title": {"stringValue": "ok"}}}]})
        else:
            self.respond([{"id": 1, "title": "ok"}])
    def do_GET(self):
        self.receive("GET")
    def do_POST(self):
        self.receive("POST")
    def do_PATCH(self):
        self.receive("PATCH")
    def do_DELETE(self):
        self.receive("DELETE")

def request(port, method="GET", body=None, cookie=None):
    h = {"Host": "127.0.0.1:" + str(port)}
    if body is not None:
        h["Content-Type"] = "application/x-www-form-urlencoded"
    if cookie:
        h["Cookie"] = cookie
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    conn.request(method, "/", body=body, headers=h)
    res = conn.getresponse()
    data = res.status, dict(res.getheaders()), res.read().decode()
    conn.close()
    return data

def app_source(engine, statement, update, auth=False):
    cfg = ('database { engine: "' + engine + '" '
           'dsn_env: "NQ_TEST_PROVIDER" key_env: "NQ_TEST_KEY"')
    if auth:
        cfg += ' auth_env: "NQ_TEST_USER"'
    cfg += ' }\n'
    return (cfg + 'query items = "' + statement + '"\n'
        'route "/" { main { h1 { "Provider contract" } p { items }'
        ' form on.submit { db.execute("' + update + '", title:form.title) }'
        ' { input name: "title" type: "text" {} button type: "submit" { "Save" } } } }\n')

def start_app(folder, state, env):
    port = free_port()
    proc = subprocess.Popen([binary, "serve-app", str(folder), str(port), str(state)],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=env)
    for _ in range(80):
        if proc.poll() is not None:
            raise AssertionError("native application unexpectedly exited: " + proc.stderr.read().decode())
        try:
            result = request(port)
            return proc, port, result
        except OSError:
            time.sleep(.06)
    proc.terminate()
    raise AssertionError("native app did not start")

def stop(proc):
    proc.terminate()
    try:
        _, logs = proc.communicate(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        _, logs = proc.communicate()
    assert b"sb_publishable" not in logs
    assert b"test-user-token" not in logs

def submit(port, first, title):
    status, headers, page = first
    assert status == 200, (status, page)
    cookie = headers["Set-Cookie"].split(";")[0]
    csrf = re.search(r'name="_nq_csrf" value="([^"]+)"', page).group(1)
    action = re.search(r'name="_nq_action" value="([^"]+)"', page).group(1)
    body = urllib.parse.urlencode({"_nq_csrf": csrf, "_nq_action": action, "title": title})
    return request(port, "POST", body, cookie)

with TemporaryDirectory() as tmp:
    root = Path(tmp)
    cert, key = root / "certificate.pem", root / "key.pem"
    subprocess.run(["openssl", "req", "-new", "-newkey", "rsa:2048", "-x509",
                    "-sha256", "-nodes", "-days", "1", "-keyout", str(key),
                    "-out", str(cert), "-subj", "/CN=localhost",
                    "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    provider = ThreadingHTTPServer(("127.0.0.1", 0), Provider)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(str(cert), str(key))
    provider.socket = ctx.wrap_socket(provider.socket, server_side=True)
    thread = Thread(target=provider.serve_forever, daemon=True)
    thread.start()
    base = "https://127.0.0.1:" + str(provider.server_port)
    env = dict(os.environ, NOQERI_WEB_CLOUD_CA_BUNDLE=str(cert), NO_PROXY="localhost,127.0.0.1",
               HTTPS_PROXY="", HTTP_PROXY="", ALL_PROXY="",
               NQ_TEST_KEY="sb_publishable_test-key", NQ_TEST_USER="test-user-token")
    folder = root / "site"
    folder.mkdir()
    try:
        # A publishable API key must NOT be treated as a signed-in user JWT.
        env["NQ_TEST_PROVIDER"] = base
        (folder / "main.noqeri").write_text(app_source(
            "supabase", "GET /rest/v1/tasks?select=id,title", "POST /rest/v1/tasks"))
        proc, port, first = start_app(folder, root / "state1.nqkv", env)
        try:
            assert submit(port, first, "a'b <x>")[0] == 303
            requests = list(Provider.requests)
            assert requests[0][2]["apikey"] == "sb_publishable_test-key"
            assert "Authorization" not in requests[0][2]
            assert json.loads(requests[-1][3]) == {"title": "a'b <x>"}
            assert requests[-1][2]["Content-Type"] == "application/json"
        finally:
            stop(proc)

        # With a separate signed-in JWT the user authorization is preserved for RLS.
        Provider.requests.clear()
        (folder / "main.noqeri").write_text(app_source(
            "supabase", "GET /rest/v1/tasks?select=id", "POST /rest/v1/tasks", auth=True))
        proc, port, first = start_app(folder, root / "state2.nqkv", env)
        try:
            assert first[0] == 200
            assert Provider.requests[0][2]["Authorization"] == "Bearer test-user-token"
            assert Provider.requests[0][2]["apikey"] == "sb_publishable_test-key"
        finally:
            stop(proc)

        # A service-role/secret key cannot be paired with user JWT and bypass RLS.
        Provider.requests.clear()
        env["NQ_TEST_KEY"] = "sb_secret_test-secret"
        proc, port, first = start_app(folder, root / "state3.nqkv", env)
        try:
            assert first[0] == 503
            assert Provider.requests == []
        finally:
            stop(proc)
        env["NQ_TEST_KEY"] = "firestore-id-token"

        # The Firestore adapter uses tagged REST values and conditional update masks.
        Provider.requests.clear()
        env["NQ_TEST_PROVIDER"] = base + "/v1/projects/demo/databases/(default)/documents"
        (folder / "main.noqeri").write_text(app_source(
            "firestore", "GET /notes", "PATCH /notes/one"))
        proc, port, first = start_app(folder, root / "state4.nqkv", env)
        try:
            assert first[0] == 200
            assert submit(port, first, "Hello")[0] == 303
            method, path, headers, payload = Provider.requests[-1]
            assert method == "PATCH", method
            assert path.endswith("/notes/one?currentDocument.exists=true&updateMask.fieldPaths=title"), path
            assert headers["Authorization"] == "Bearer firestore-id-token"
            assert json.loads(payload) == {"fields":{"title":{"stringValue":"Hello"}}}
        finally:
            stop(proc)

        # Trust verification is mandatory; removing the test CA must fail closed.
        env.pop("NOQERI_WEB_CLOUD_CA_BUNDLE")
        Provider.requests.clear()
        proc, port, first = start_app(folder, root / "state5.nqkv", env)
        try:
            assert first[0] == 503, first
            assert Provider.requests == [], "request must never reach provider with untrusted certificate"
        finally:
            stop(proc)
    finally:
        provider.shutdown()
        provider.server_close()
        thread.join(timeout=4)
print("PASS: native HTTPS Supabase RLS headers, Firestore tagged writes, auth isolation and TLS fail-closed")
