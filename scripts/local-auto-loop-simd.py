#!/usr/bin/env python3
"""Local scalar-vs-auto-SSE2 loop differential and performance measurements.

Build the CMake noqeri-auto-loop-simd-emitter target before invoking. No
GitHub Actions, no benchmark result fabricated, both executables verified.
"""
import argparse
import hashlib
import json
import math
import os
import platform
import statistics
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--emitter", default="build-local-perf/noqeri-auto-loop-simd-emitter")
parser.add_argument("--output", default="build-local-perf/auto-loop-simd.json")
parser.add_argument("--samples", type=int, default=25)
args = parser.parse_args()
if args.samples < 15:
    parser.error("at least 15 samples are required")
if platform.system() != "Linux" or platform.machine() not in ("x86_64","AMD64"):
    parser.error("requires native Linux x86-64 SSE2")
emitter = Path(args.emitter)
if not emitter.is_absolute():
    emitter = (ROOT / emitter).resolve()
if not emitter.is_file():
    parser.error(f"build the standalone emitter first: {emitter}")

outpath = Path(args.output)
if not outpath.is_absolute():
    outpath = ROOT / outpath
outpath.parent.mkdir(parents=True,exist_ok=True)

def check(command, **kwargs):
    return subprocess.run(
        [str(c) for c in command], cwd=ROOT, check=True,
        capture_output=True, text=True, timeout=120, **kwargs
    )

programs = {}
asm_sha = {}
for name, disable in (("scalar", True),("vector", False)):
    assembly = outpath.parent / ("auto-loop-"+name+".s")
    obj = outpath.parent / ("auto-loop-"+name+".o")
    binary = outpath.parent / ("auto-loop-"+name)
    env = dict(os.environ)
    env["NOQERI_NATIVE_LOOP_SIMD"] = "0" if disable else "1"
    env.pop("NOQERI_EXPERIMENTAL_SIMD",None)
    check([emitter,assembly],env=env)
    check(["cc","-c",assembly,"-o",obj])
    check(["cc","-std=c11","-O2",
           ROOT / "tests/cpp/auto_loop_simd_host.c",obj,"-o",binary])
    programs[name] = binary
    asm_sha[name] = hashlib.sha256(assembly.read_bytes()).hexdigest()

def run(name):
    start=time.perf_counter_ns()
    p=check([programs[name]])
    elapsed=(time.perf_counter_ns()-start)/1e6
    if p.stdout != "6400000\n":
        raise RuntimeError(f"{name} returned incorrect output {p.stdout!r}")
    return elapsed

for label in programs:
    run(label)
for _ in range(3):
    for label in programs:
        run(label)
samples={name:[] for name in programs}
for i in range(args.samples):
    names=("scalar","vector") if i%2==0 else ("vector","scalar")
    for name in names:
        samples[name].append(run(name))

def report(values):
    ordered=sorted(values)
    return {
        "raw_ms":values,
        "median_ms":statistics.median(values),
        "p95_ms":ordered[math.ceil(len(values)*.95)-1],
    }
measurements={name:report(raw) for name,raw in samples.items()}
speedup=measurements["scalar"]["median_ms"]/measurements["vector"]["median_ms"]
output={
    "execution":"local-x86_64-sse2-standalone",
    "host":platform.platform(),
    "samples":args.samples,
    "warmups":3,
    "matched_output":"6400000\\n",
    "loop_groups":32,
    "simd_default":"on for proven 4+ private pairs, NOQERI_NATIVE_LOOP_SIMD=0 disables",
    "source_hashes":asm_sha,
    "measurements":measurements,
    "median_speedup":speedup,
    "note":"This one private-stack kernel does not prove full loop auto-vectorization or parity with C.",
}
outpath.write_text(json.dumps(output,indent=2)+"\n")
print(json.dumps(output,indent=2))
