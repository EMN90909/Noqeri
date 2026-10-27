#!/usr/bin/env python3
"""Reproducible Noqeri native HTTP latency/throughput gate (stdlib; no Node.js).

Measures the loopback *development* renderer only, not Internet/TLS readiness.
Budgets are configurable for stable CI and stricter release hardware.
"""
from concurrent.futures import ThreadPoolExecutor
from contextlib import suppress
import http.client
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

binary = str(Path(sys.argv[1]).resolve())
project = str(Path(sys.argv[2]).resolve())
count = int(os.getenv("NOQERI_PERF_SAMPLES", "120"))
p95_budget = float(os.getenv("NOQERI_PERF_MAX_P95_MS", "250"))
parallel_budget = float(os.getenv("NOQERI_PERF_MAX_CONCURRENT_P95_MS", "500"))
rps_budget = float(os.getenv("NOQERI_PERF_MIN_RPS", "60"))
assert 20 <= count <= 10000

def percentile(samples, fraction):
    samples = sorted(samples)
    return round(samples[min(len(samples)-1, max(0, int(len(samples)*fraction+.99)-1))], 3)

def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]

def get(port, cookie=""):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=4)
    start = time.perf_counter()
    try:
        connection.request("GET", "/", headers={"Cookie":cookie} if cookie else {})
        response = connection.getresponse()
        body = response.read()
        return ((time.perf_counter()-start)*1000, response.status,
                dict(response.getheaders()), body)
    finally:
        connection.close()

def measure(reactive, temp):
    port = free_port()
    environment = dict(os.environ)
    if reactive:
        environment["NOQERI_WEB_REACTIVE"] = "1"
    else:
        environment.pop("NOQERI_WEB_REACTIVE", None)
    with (temp/("reactive.log" if reactive else "server.log")).open("w") as log:
        process = subprocess.Popen(
            [binary, "serve-app", project, str(port), str(temp/"state.nqkv")],
            stdout=subprocess.DEVNULL, stderr=log, env=environment)
        try:
            for _ in range(100):
                if process.poll() is not None:
                    raise AssertionError("native server exited before startup")
                with suppress(OSError, http.client.HTTPException):
                    _, status, headers, body = get(port)
                    if status == 200:
                        break
                time.sleep(.05)
            else:
                raise AssertionError("native server did not start")
            assert b"Noqeri native interactive web" in body
            assert headers["Cache-Control"] == "no-store"
            cookie = headers.get("Set-Cookie", "").split(";", 1)[0]
            assert cookie.startswith("_nq_sid=")
            for _ in range(15):
                assert get(port, cookie)[1] == 200
            serial = []
            for _ in range(count):
                ms, code, _, data = get(port, cookie)
                assert code == 200 and b"Your clicks:" in data
                serial.append(ms)
            def parallel_request(_):
                ms, code, _, data = get(port, cookie)
                assert code == 200 and b"Your clicks:" in data
                return ms
            start = time.perf_counter()
            with ThreadPoolExecutor(max_workers=8) as pool:
                parallel = list(pool.map(parallel_request, range(count)))
            seconds = time.perf_counter() - start
            report = {
                "mode": "reactive" if reactive else "no_js",
                "samples": count,
                "p50_ms": percentile(serial,.50),
                "p95_ms": percentile(serial,.95),
                "concurrent_8_p50_ms": percentile(parallel,.50),
                "concurrent_8_p95_ms": percentile(parallel,.95),
                "concurrent_8_rps": round(count/seconds,1)}
            assert report["p95_ms"] < p95_budget, report
            assert report["concurrent_8_p95_ms"] < parallel_budget, report
            assert report["concurrent_8_rps"] > rps_budget, report
            return report
        finally:
            process.terminate()
            try:
                process.wait(timeout=4)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

with tempfile.TemporaryDirectory(prefix="noqeri-native-perf-") as folder:
    temp = Path(folder)
    results = [measure(False,temp), measure(True,temp)]
report = {"benchmark":"native-loopback-get", "transport":"HTTP/1.1 fresh socket per request",
          "requires_node":False, "results":results}
print("NOQERI_PERFORMANCE "+json.dumps(report, sort_keys=True))
output = os.getenv("NOQERI_PERF_OUTPUT")
if output:
    Path(output).write_text(json.dumps(report,indent=2)+"\n")
