#!/usr/bin/env python3
"""Qualify extended validation, progressive client mode, token auth and durable private state."""
import http.client
import os
from pathlib import Path
import re
import socket
import stat
import subprocess
import sys
import tempfile
import time
from urllib.parse import urlencode

binary = str(Path(sys.argv[1]).resolve())

def port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]

def request(port, method="GET", path="/", data=None, cookie=None, token=None):
    c = http.client.HTTPConnection("127.0.0.1", port, timeout=4)
    headers = {}
    if data is not None:
        headers["Content-Type"] = "application/x-www-form-urlencoded"
    if cookie:
        headers["Cookie"] = cookie
    if token:
        headers["Authorization"] = "Bearer " + token
    c.request(method, path, body=data, headers=headers)
    r = c.getresponse()
    result = (r.status, dict(r.getheaders()), r.read().decode())
    c.close()
    return result

def launch(project, state, env):
    p = port()
    proc = subprocess.Popen([binary, "serve-app", str(project), str(p), str(state)],
                            env={**os.environ, **env}, stdout=subprocess.DEVNULL,
                            stderr=subprocess.PIPE)
    for _ in range(80):
        if proc.poll() is not None:
            raise AssertionError(proc.stderr.read().decode())
        try:
            if request(p, path="/theme.css", token=env.get("NOQERI_WEB_BEARER_TOKEN"))[0] == 200:
                return proc, p
        except OSError:
            time.sleep(.05)
    stop(proc)
    raise AssertionError("native app did not start")

def stop(proc):
    proc.terminate()
    try:
        proc.communicate(timeout=4)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.communicate()

with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    project = root / "site"
    project.mkdir()
    (project / "main.noqeri").write_text("""
let submissions = 0
persist last_email = "none"
route "/" {
  main {
    p { submissions }
    p { last_email }
    form on.submit {
      validateEmail(email)
      validateNumber(age, 18, 120)
      last_email = form.email
      submissions = submissions + 1
    } {
      input name: "email" type: "email" required: true minlength: 5 {}
      input name: "age" type: "number" min: 18 max: 120 {}
      button type: "submit" { "Save" }
    }
  }
}
""")
    state = root / "state.nqkv"
    proc, p = launch(project, state, {"NOQERI_WEB_REACTIVE": "1"})
    try:
        code, headers, page = request(p)
        assert code == 200, (code,page)
        sid = headers["Set-Cookie"].split(";",1)[0]
        assert 'data-nq-reactive="1"' in page
        assert '<script defer src="/__nq/reactive.js"></script>' in page
        assert 'script-src &#39;' not in page
        code, headers, client = request(p, path="/__nq/reactive.js")
        assert code == 200 and "text/javascript" in headers["Content-Type"]
        assert "FormData(form)" in client and "noqeri:updated" in client
        assert 'required' in page and 'minlength="5"' in page
        csrf = re.search(r'name="_nq_csrf" value="([^"]+)"', page).group(1)
        action = re.search(r'name="_nq_action" value="([^"]+)"', page).group(1)
        def submit(email, age):
            return request(p, "POST", data=urlencode({
                "_nq_action": action,"_nq_csrf": csrf,"email": email,"age": age
            }), cookie=sid)
        assert submit("bademail", "28")[0] == 422
        assert submit("ada@example.com", "17")[0] == 422
        assert submit("ada@example.com", "1O")[0] == 422
        assert "0" in request(p, cookie=sid)[2] and not state.exists()
        assert submit("ada@example.com", "28")[0] == 303
        assert "ada@example.com" in request(p, cookie=sid)[2]
        assert state.is_file()
        assert stat.S_IMODE(state.stat().st_mode) == 0o600
        assert submit("ada@example.com", "28")[0] == 403
    finally:
        stop(proc)
    proc, p = launch(project, state, {})
    try:
        _,_,page=request(p)
        assert "ada@example.com" in page
        assert "/__nq/reactive.js" not in page
        assert request(p,path="/__nq/reactive.js")[0] == 404
    finally:
        stop(proc)
    token = "a" * 48
    proc, p = launch(project, state, {"NOQERI_WEB_BEARER_TOKEN": token})
    try:
        assert request(p)[0] == 401
        assert request(p,token="wrong")[0] == 401
        assert request(p,token=token)[0] == 200
        assert request(p,path="/theme.css",token=token)[0] == 200
    finally:
        stop(proc)
print("PASS: reactive progressive mode, form validation, private durable persistence, bearer auth")
