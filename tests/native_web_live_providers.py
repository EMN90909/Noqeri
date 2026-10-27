#!/usr/bin/env python3
"""Opt-in LIVE smoke against dedicated provider test resources. Read-only; no secrets printed.

Required (to run all three):
  NOQERI_LIVE_SUPABASE_URL, NOQERI_LIVE_SUPABASE_KEY, NOQERI_LIVE_SUPABASE_TABLE
  NOQERI_LIVE_FIRESTORE_URL, NOQERI_LIVE_FIRESTORE_TOKEN, NOQERI_LIVE_FIRESTORE_COLLECTION
  NOQERI_LIVE_NEON_DSN
Set NOQERI_LIVE_REQUIRE_ALL=1 to fail if any provider is not configured.
"""
import http.client
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

binary = str(Path(sys.argv[1]).resolve())
def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]

def checked_name(name):
    if not name or len(name)>62 or not all(ch.isascii() and (ch.isalnum() or ch=='_') for ch in name):
        raise ValueError("provider test resource must have an alphanumeric or underscore name")
    return name

def run_provider(name, configuration, statement, env):
    with tempfile.TemporaryDirectory() as temp:
        directory = Path(temp)
        (directory/"main.noqeri").write_text(
            'database {'+configuration+'}\n'
            'query providerResult = "'+statement+'"\n'
            'route "/" { main { pre { providerResult } } }\n')
        port = free_port()
        proc = subprocess.Popen([binary,"serve-app",str(directory),str(port),
            str(directory/"session.nqkv")],env=env,
            stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        try:
            for _ in range(90):
                if proc.poll() is not None:
                    raise RuntimeError(name+" failed to start")
                try:
                    connection = http.client.HTTPConnection("127.0.0.1",port,timeout=4)
                    connection.request("GET","/")
                    response = connection.getresponse()
                    status = response.status
                    _ = response.read()
                    connection.close()
                    if status!=200:
                        raise AssertionError(name+" returned HTTP "+str(status)+" (check server credentials, policies, and TLS)")
                    print("LIVE PASS: "+name+" read-only native HTTPS/SQL connection")
                    return
                except ConnectionRefusedError:
                    time.sleep(.07)
        finally:
            proc.terminate()
            try:
                _,logs = proc.communicate(timeout=4)
            except subprocess.TimeoutExpired:
                proc.kill()
                _,logs = proc.communicate()
            for secret in ("NOQERI_LIVE_SUPABASE_KEY","NOQERI_LIVE_FIRESTORE_TOKEN","NOQERI_LIVE_NEON_DSN"):
                value = env.get(secret,"")
                if value and value.encode() in logs:
                    raise AssertionError("provider logs included sensitive configuration")

env = os.environ.copy()
cases = [
    ("Supabase",("NOQERI_LIVE_SUPABASE_URL","NOQERI_LIVE_SUPABASE_KEY","NOQERI_LIVE_SUPABASE_TABLE")),
    ("Cloud Firestore",("NOQERI_LIVE_FIRESTORE_URL","NOQERI_LIVE_FIRESTORE_TOKEN","NOQERI_LIVE_FIRESTORE_COLLECTION")),
    ("Neon",("NOQERI_LIVE_NEON_DSN",))
]
missing=[]
for name,required in cases:
    if not all(env.get(k) for k in required):
        print("LIVE NOT RUN: "+name+" (dedicated test credentials/resources missing)")
        missing.append(name)
        continue
    if name=="Supabase":
        table=checked_name(env["NOQERI_LIVE_SUPABASE_TABLE"])
        run_provider(name,
            ' engine: "supabase" dsn_env: "NOQERI_LIVE_SUPABASE_URL" key_env: "NOQERI_LIVE_SUPABASE_KEY" ',
            "GET /"+table+"?select=*&limit=1",env)
    elif name=="Cloud Firestore":
        collection=checked_name(env["NOQERI_LIVE_FIRESTORE_COLLECTION"])
        run_provider(name,
            ' engine: "firestore" dsn_env: "NOQERI_LIVE_FIRESTORE_URL" key_env: "NOQERI_LIVE_FIRESTORE_TOKEN" ',
            "GET /"+collection,env)
    else:
        run_provider(name,
            ' engine: "neon" dsn_env: "NOQERI_LIVE_NEON_DSN" ',
            "SELECT 1 AS alive",env)
if missing and env.get("NOQERI_LIVE_REQUIRE_ALL")=="1":
    raise SystemExit("LIVE QUALIFICATION INCOMPLETE: missing provider test settings")
