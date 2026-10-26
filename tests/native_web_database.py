#!/usr/bin/env python3
"""End-to-end native database integration: SQLite is real; NoeSQL dispatch mock verifies safe argv."""
import http.client
import html
import os
from pathlib import Path
import re
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time
import urllib.parse

binary = str(Path(sys.argv[1]).resolve())

def unused_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]

def req(port, method, path, body=None, cookie=None):
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
    headers = {}
    if body is not None:
        headers["Content-Type"] = "application/x-www-form-urlencoded"
    if cookie:
        headers["Cookie"] = cookie
    conn.request(method, path, body, headers)
    response = conn.getresponse()
    result = (response.status, dict(response.getheaders()), response.read().decode())
    conn.close()
    return result

def start(path, port, state, env=None):
    proc = subprocess.Popen([binary, "serve-app", str(path), str(port), str(state)],
                            env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    for i in range(60):
        if proc.poll() is not None:
            raise AssertionError(proc.stderr.read().decode())
        try:
            if req(port, "GET", "/theme.css")[0] == 200:
                return proc
        except OSError:
            time.sleep(.08)
    proc.terminate()
    raise AssertionError("native DB server failed to start")

def stop(proc):
    proc.terminate()
    try:
        proc.communicate(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.communicate()

def submit(port, cookie, html_body, name, field="name"):
    token = re.search(r'name="_nq_csrf" value="([^"]+)"', html_body).group(1)
    action = re.search(r'name="_nq_action" value="([^"]+)"', html_body).group(1)
    form = urllib.parse.urlencode({"_nq_action":action, "_nq_csrf":token, field:name})
    return req(port, "POST", "/", form, cookie)

with tempfile.TemporaryDirectory() as work:
    root=Path(work)
    sqlite_file=root/"records.sqlite"
    with sqlite3.connect(sqlite_file) as connection:
        connection.execute("CREATE TABLE entries(id INTEGER PRIMARY KEY, name TEXT NOT NULL)")
    project=root/"site"
    project.mkdir()
    (project/"main.noqeri").write_text('''
database {
 engine: "sqlite"
 path: "'''+str(sqlite_file)+'''"
}
query entries = "SELECT name FROM entries ORDER BY id"
route "/" {
  main {
    h1 { "SQLite native" }
    p { entries }
    form on.submit {
      db.execute("INSERT INTO entries(name) VALUES (:name)", name: form.name)
    } {
      label { "Name" }
      input name: "name" type: "text" {}
      button type: "submit" { "Add entry" }
    }
  }
}
''')
    port=unused_port()
    proc=start(project,port,root/"state.nqkv")
    try:
        status,headers,home=req(port,"GET","/")
        assert status==200,(status,home)
        cookie=headers["Set-Cookie"].split(";")[0]
        assert "SQLite native" in home
        malicious = '"; DROP TABLE entries; -- <b>&'
        status,_,_=submit(port,cookie,home,malicious)
        assert status==303,status
        status,_,after=req(port,"GET","/",cookie=cookie)
        assert status==200,(status,after)
        assert "&lt;b&gt;&amp;" in after,after
        assert "<b>" not in after
        with sqlite3.connect(sqlite_file) as connection:
            names=connection.execute("SELECT name FROM entries").fetchall()
            assert names==[(malicious,)],names
            assert connection.execute("SELECT count(*) FROM sqlite_master WHERE name='entries'").fetchone()[0]==1
    finally:
        stop(proc)

    # The NoeSQL bridge must invoke a native binary without a shell or a SQL
    # translator, passing the original .noe source and the unmodified bind.
    # This mock verifies argv protocol; real engine execution is tested in NoeSQL.
    spy=root/"fake-noesql"
    calls=root/"noesql-args.txt"
    spy.write_text("#!/usr/bin/env python3\nimport os,sys\n"
        "with open(os.environ['NOESQL_TEST_LOG'],'a') as out: out.write(repr(sys.argv[1:])+'\\n')\n"
        "print('[{\"name\":\"Native NoeSQL\"}]' if ' get ' in sys.argv[3] else 'affected=1')\n")
    spy.chmod(0o700)
    project.joinpath("main.noqeri").write_text('''
database {
 engine: "noesql"
 path: "'''+str(root/"records.noedb")+'''"
}
query names = "users get name sort name"
route "/" {
  main {
    h1 { "NoeSQL native" }
    p { names }
    form on.submit {
      db.execute("users add { name: $name }", name: form.name)
    } {
      input name: "name" type: "text" {}
      button type: "submit" { "Save" }
    }
  }
}
''')
    env=dict(os.environ, NOESQL_BIN=str(spy), NOESQL_TEST_LOG=str(calls))
    port=unused_port()
    proc=start(project,port,root/"state2.nqkv",env)
    try:
        status,headers,home=req(port,"GET","/")
        assert status==200,(status,home)
        cookie=headers["Set-Cookie"].split(";")[0]
        assert "Native NoeSQL" in home
        submitted='A " name; users delete'
        assert submit(port,cookie,home,submitted)[0]==303
    finally:
        stop(proc)
    call_log=calls.read_text()
    assert "users add { name: $name }" in call_log
    assert ("name:text:"+submitted) in call_log
    assert ".noedb" in call_log
    assert "query" not in call_log, "NoeSQL adapter should use exec for all bound statements"

    # A missing NoeSQL runtime is an error. Never silently swap to SQLite.
    env.pop("NOESQL_BIN")
    port=unused_port()
    proc=start(project,port,root/"state3.nqkv",env)
    try:
        assert req(port,"GET","/")[0]==400
    finally:
        stop(proc)

    # A .noedb source can't be mistaken for SQLite or legacy .nqdb bytes.
    project.joinpath("main.noqeri").write_text('''
database { engine: "noesql" path: "'''+str(root/"old.nqdb")+'''" }
route "/" { h1 { "invalid" } }
''')
    proc=subprocess.run([binary,"serve-app",str(project),str(unused_port()),
                         str(root/"state4.nqkv")],capture_output=True,timeout=5)
    assert proc.returncode!=0 and b".noedb" in proc.stderr

print("PASS: native SQLite CRUD and parameter safety; NoeSQL native CLI delegation and no-fallback validation")
