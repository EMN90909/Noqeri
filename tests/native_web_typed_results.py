#!/usr/bin/env python3
"""Native SQLite query result JSON preserves numbers, text and SQL NULL."""
import html
import http.client
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

binary=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory() as td:
    root=Path(td)
    project=root/"app";project.mkdir()
    database=(root/"results.sqlite").as_posix()
    (project/"main.noqeri").write_text('''
database { engine: "sqlite" path: "'''+database+'''" }
query typed = "SELECT 7 AS total, 1.5 AS ratio, NULL AS missing, 'ok' AS label"
route "/" {
  main { p { typed } }
}
''')
    with socket.socket() as s:
        s.bind(("127.0.0.1",0))
        port=s.getsockname()[1]
    server=subprocess.Popen([str(binary),"serve-app",str(project),str(port),
                              str(root/"session.nqkv")],
                            stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        for attempt in range(70):
            if server.poll() is not None:
                raise AssertionError(server.stderr.read().decode())
            try:
                connection=http.client.HTTPConnection("127.0.0.1",port,timeout=4)
                connection.request("GET","/")
                response=connection.getresponse()
                status=response.status
                body=html.unescape(response.read().decode())
                connection.close()
                if status==200:break
                raise AssertionError(f"Unexpected response {status}: {body}")
            except OSError:time.sleep(.07)
        else:raise AssertionError("typed query server did not start")
        assert '"total":7' in body,body
        assert '"ratio":1.5' in body,body
        assert '"missing":null' in body,body
        assert '"label":"ok"' in body,body
        assert '"missing":""' not in body and '"total":"7"' not in body
    finally:
        server.terminate()
        try:server.communicate(timeout=4)
        except subprocess.TimeoutExpired:
            server.kill();server.communicate()
print("PASS: native SQL JSON preserves int, float, SQL NULL and text")
