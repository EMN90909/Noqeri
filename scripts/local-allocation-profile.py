#!/usr/bin/env python3
"""Manual same-host allocation performance measurements; no GitHub Actions.

Example:
  python3 scripts/local-allocation-profile.py \
    --candidate build-local-perf/noqeri-owned-alloc-bench \
    --output build-local-perf/allocation-profile.json
Use --baseline /path/to/previous-binary for same-host A/B.
"""
import argparse
import hashlib
import json
import math
import platform
import statistics
import subprocess
import time
from pathlib import Path

MODES = ("int-list", "owned-value", "handles", "threaded")
parser = argparse.ArgumentParser()
parser.add_argument("--candidate", required=True, type=Path)
parser.add_argument("--baseline", type=Path)
parser.add_argument("--output", type=Path)
parser.add_argument("--samples", type=int, default=15)
args = parser.parse_args()
if args.samples < 15:
    parser.error("at least 15 measured samples are required")
programs = {"candidate": args.candidate.resolve()}
if args.baseline:
    programs["baseline"] = args.baseline.resolve()
for name, binary in programs.items():
    if not binary.is_file():
        parser.error(f"{name} binary does not exist: {binary}")
data = {}
for mode in MODES:
    samples = {name: [] for name in programs}
    expected = None
    def run(name):
        start = time.perf_counter_ns()
        done = subprocess.run(
            [str(programs[name]), mode], capture_output=True,
            text=True, check=True, timeout=60
        )
        ms = (time.perf_counter_ns()-start)/1e6
        if expected is not None and done.stdout != expected:
            raise RuntimeError(f"{mode}: {name} changed its checked output")
        if not done.stdout.strip().isdigit():
            raise RuntimeError(f"{mode}: {name} invalid checksum {done.stdout!r}")
        return ms, done.stdout
    for name in programs:
        _, out = run(name)
        if expected is None:
            expected = out
        if out != expected:
            raise RuntimeError(f"{mode}: output mismatch")
    for _ in range(3):
        for name in programs:
            run(name)
    names = list(programs)
    for i in range(args.samples):
        for name in (names if i % 2 == 0 else names[::-1]):
            ms, _ = run(name)
            samples[name].append(ms)
    def summarize(raw):
        ordered = sorted(raw)
        return {
            "raw_ms": raw,
            "median_ms": statistics.median(raw),
            "p95_ms": ordered[math.ceil(0.95*len(ordered))-1],
        }
    data[mode] = {
        "checksum": expected.strip(),
        "measurements": {k:summarize(v) for k,v in samples.items()},
    }
    if args.baseline:
        base = data[mode]["measurements"]["baseline"]["median_ms"]
        new = data[mode]["measurements"]["candidate"]["median_ms"]
        data[mode]["speedup_vs_baseline"] = base / new
cpu = "unknown"
cpuinfo = Path("/proc/cpuinfo")
if cpuinfo.exists():
    for line in cpuinfo.read_text(errors="replace").splitlines():
        if line.startswith("model name"):
            cpu=line.split(":", 1)[1].strip()
            break
report = {
    "execution":"local-native-standalone",
    "machine":platform.machine(),"platform":platform.platform(),
    "cpu":cpu,"python":platform.python_version(),
    "sample_count":args.samples,"warmups":3,
    "metrics":"wall-clock process launch + checked runtime",
    "binaries":{
        name:{"path":str(p),"sha256":hashlib.sha256(p.read_bytes()).hexdigest()}
        for name,p in programs.items()
    },
    "workloads":data,
    "caveat":"This tests owned collection API paths; it does not measure GC pauses, SSA or C parity.",
}
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text(json.dumps(report,indent=2)+"\n")
print(json.dumps(report,indent=2))
