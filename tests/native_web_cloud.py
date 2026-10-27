#!/usr/bin/env python3
"""No live secrets: exercise native cloud parsing, startup checks, fail-closed HTTPS."""
import http.client
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

binary = str(Path(sys.argv[1]).resolve())

def port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]

def get(p):
    conn = http.client.HTTPConnection("127.0.0.1", p, timeout=3)
    conn.request("GET", "/", headers={"Host": "127.0.0.1:"+str(p)})
    response = conn.getresponse()
    result = response.status, response.read().decode()
    conn.close()
    return result

with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    site = root / "app"
    site.mkdir()
    config = '''
database {
 engine: "supabase"
 dsn_env: "NOQERI_CLOUD_TEST_URL"
 key_env: "NOQERI_CLOUD_TEST_TOKEN"
}
query records = "GET /records?select=id,name"
route "/" { main { p { records } } }
'''
    (site / "main.noqeri").write_text(config)
    env = dict(os.environ, NOQERI_CLOUD_TEST_URL="http://127.0.0.1:1",
               NOQERI_CLOUD_TEST_TOKEN="do-not-log-this-token")
    p = port()
    proc = subprocess.Popen([binary, "serve-app", str(site), str(p), str(root/"state.nqkv")],
                            env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        for _ in range(80):
            if proc.poll() is not None:
                raise AssertionError(proc.stderr.read().decode())
            try:
                status, body = get(p)
                break
            except OSError:
                time.sleep(.05)
        else:
            raise AssertionError("native cloud app failed to listen")
        assert status == 503, (status, body)
        assert "do-not-log" not in body
    finally:
        proc.terminate()
        try:
            _, log = proc.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            _, log = proc.communicate()
        assert b"do-not-log-this-token" not in log

    (site / "main.noqeri").write_text(config.replace(' key_env: "NOQERI_CLOUD_TEST_TOKEN"', ''))
    attempt = subprocess.run([binary, "serve-app", str(site), str(port()),
                             str(root/"state2.nqkv")], env=env,
                             capture_output=True, timeout=5)
    assert attempt.returncode != 0 and b"key_env" in attempt.stderr

    (site / "main.noqeri").write_text(
        config.replace('query records = "GET /records?select=id,name"',
                       'query records = "POST /records"'))
    attempt = subprocess.run([binary, "serve-app", str(site), str(port()),
                             str(root/"state3.nqkv")], env=env,
                             capture_output=True, timeout=5)
    assert attempt.returncode != 0 and b"GET" in attempt.stderr

    (site / "main.noqeri").write_text(config.replace('engine: "supabase"',
                     'engine: "firebase"').replace('/records?select=id,name','/records.json'))
    attempt = subprocess.Popen([binary, "serve-app", str(site), str(port()),
                                str(root/"state4.nqkv")], env=env,
                                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        # The server must start with a Firebase declaration even without a live provider.
        time.sleep(.15)
        assert attempt.poll() is None, attempt.stderr.read().decode() if attempt.poll() is not None else ""
    finally:
        attempt.terminate()
        attempt.communicate(timeout=3)

    # Render a real native cloud form without contacting the remote provider;
    # HTML-required attributes are only client hints, while validate() is
    # independently enforced on POST before the database write.
    (site / "main.noqeri").write_text("""
database { engine: "supabase" dsn_env: "NOQERI_CLOUD_TEST_URL"
 key_env: "NOQERI_CLOUD_TEST_TOKEN" }
route "/" {
 main {
  form on.submit {
   validate(title, 2, 40)
   db.execute("POST /items", title: form.title)
  } {
   input name: "title" type: "text" required: true minlength: 2 maxlength: 40 {}
   button type: "submit" { "Save" }
  }
 }
}
""")
    p = port()
    proc = subprocess.Popen([binary, "serve-app", str(site), str(p),
                             str(root / "state5.nqkv")], env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        for _ in range(80):
            if proc.poll() is not None:
                raise AssertionError(proc.stderr.read().decode())
            try:
                conn = http.client.HTTPConnection("127.0.0.1", p, timeout=3)
                conn.request("GET", "/")
                reply = conn.getresponse()
                html = reply.read().decode()
                cookie = dict(reply.getheaders()).get("Set-Cookie", "").split(";")[0]
                conn.close()
                break
            except OSError:
                time.sleep(.05)
        else:
            raise AssertionError("cloud form route did not start")
        assert reply.status == 200, html
        assert 'required' in html and 'minlength="2"' in html, html
        import re
        import urllib.parse
        action = re.search(r'name="_nq_action" value="([^"]+)"', html).group(1)
        csrf = re.search(r'name="_nq_csrf" value="([^"]+)"', html).group(1)
        body = urllib.parse.urlencode({"_nq_action":action,"_nq_csrf":csrf,"title":"x"})
        conn = http.client.HTTPConnection("127.0.0.1", p, timeout=3)
        conn.request("POST", "/", body, {"Cookie":cookie,
                     "Content-Type":"application/x-www-form-urlencoded"})
        res = conn.getresponse()
        response = res.read().decode()
        conn.close()
        assert res.status == 422 and "validation" in response.lower(), (res.status,response)
    finally:
        proc.terminate()
        proc.communicate(timeout=3)

    output = subprocess.check_output([binary, "drivers"], text=True)
    for name in ("neon", "supabase", "firebase"):
        assert name+": " in output, output

print("PASS: native cloud configuration, read-only queries, HTTPS failure isolation and secret redaction")
