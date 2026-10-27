#!/usr/bin/env python3
"""Run against a REAL HTTPS deployment, not a loopback-only perf fixture.

Usage:
 python3 tests/native_web_deployment_smoke.py https://example.com --protected /members

No credentials are used; requests are read-only. This validates ingress/TLS
and captures end-to-end concurrent response latencies.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import http.client
import json
import ssl
import time
from urllib.parse import urlsplit

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("origin",help="absolute HTTPS origin of deployed Noqeri service")
parser.add_argument("--protected",help="path that MUST reject anonymous access")
parser.add_argument("--samples",type=int,default=32)
parser.add_argument("--workers",type=int,default=8)
parser.add_argument("--max-p95-ms",type=float,default=2000)
parser.add_argument("--ca-file",help="optional trusted CA PEM for staging")
args=parser.parse_args()
parsed=urlsplit(args.origin)
if(parsed.scheme!="https" or not parsed.hostname or parsed.username or parsed.password or
   parsed.path not in ("","/") or parsed.query or parsed.fragment):
    parser.error("origin must be an HTTPS origin without credentials, path or query")
if(args.protected and (not args.protected.startswith("/") or
   args.protected.startswith("//") or ".." in args.protected)):
    parser.error("protected path must be a safe absolute path")
if not (16<=args.samples<=512 and 1<=args.workers<=32 and args.max_p95_ms>0):
    parser.error("samples 16..512, workers 1..32 and positive p95 budget required")
context=ssl.create_default_context(cafile=args.ca_file)
def request(path):
    started=time.perf_counter()
    connection=http.client.HTTPSConnection(parsed.hostname,parsed.port or 443,
        timeout=12,context=context)
    try:
        connection.request("GET",path,headers={"Accept":"text/html","User-Agent":"NoqeriDeploymentSmoke/1"})
        response=connection.getresponse()
        data=response.read(1024*1024+1)
        if len(data)>1024*1024:raise AssertionError("response exceeds 1 MiB")
        headers={k.lower():v for k,v in response.getheaders()}
        return (time.perf_counter()-started)*1000,response.status,headers
    finally:connection.close()
latency,status,headers=request("/")
assert status in (200,301,302,303,307,308),("unexpected status",status)
assert headers.get("strict-transport-security","").startswith("max-age="), "HSTS missing at TLS edge"
assert headers.get("x-content-type-options","").lower()=="nosniff","X-Content-Type-Options missing"
assert headers.get("x-frame-options","").upper()=="DENY" or "frame-ancestors 'none'" in headers.get("content-security-policy",""),"frame protection missing"
if status in (301,302,303,307,308):
    redirect=headers.get("location","")
    if redirect.startswith("http://"):
        raise AssertionError("HTTPS redirect downgrade")
if args.protected:
    _,status,headers=request(args.protected)
    assert status in (401,403,302,303),("protected route is accessible anonymously",status)
    if status in (302,303) and headers.get("location","").startswith("http://"):
        raise AssertionError("authentication redirect downgrades HTTPS")
with ThreadPoolExecutor(max_workers=args.workers) as pool:
    measured=list(pool.map(lambda _:request("/"),range(args.samples)))
allowed=(200,301,302,303,307,308,401,403,429)
assert all(code in allowed for _,code,_ in measured),("unexpected response statuses",set(x[1] for x in measured))
latencies=sorted(item[0] for item in measured)
p50=latencies[min(len(latencies)-1,int(len(latencies)*.50+.99)-1)]
p95=latencies[min(len(latencies)-1,int(len(latencies)*.95+.99)-1)]
result={"origin":args.origin,"samples":args.samples,"concurrency":args.workers,
        "p50_ms":round(p50,2),"p95_ms":round(p95,2),
        "status_counts":{str(code):sum(1 for _,status,_ in measured if status==code)
                         for code in sorted(set(status for _,status,_ in measured))}}
print("NOQERI_DEPLOYMENT_SMOKE "+json.dumps(result,sort_keys=True))
assert p95<args.max_p95_ms,f"p95 {p95:.2f} ms exceeds {args.max_p95_ms:.2f} ms"
print("PASS: verified HTTPS ingress headers, anonymous access boundaries and end-to-end response budget")
