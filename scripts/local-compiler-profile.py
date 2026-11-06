#!/usr/bin/env python3
"""Repeatable local compiler throughput comparison (no GitHub Actions).

Compile a fixed 501-function source before timing, then alternate baseline and
candidate compiler processes. Separate *compile time* from executable runtime.
"""
import argparse
import hashlib
import json
import math
import platform
import statistics
import subprocess
import tempfile
import time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument("--candidate", default="build-local-perf/noqeri")
parser.add_argument("--baseline")
parser.add_argument("--output", default="build-local-perf/compiler-profile.json")
parser.add_argument("--samples", type=int, default=25)
args=parser.parse_args()
if args.samples<15:
    parser.error("at least 15 samples required")
programs={"candidate":Path(args.candidate)}
if args.baseline:
    programs["baseline"]=Path(args.baseline)
for name,path in programs.items():
    if not path.is_absolute():
        programs[name]=(ROOT/path).resolve()
    if not programs[name].is_file():
        parser.error(f"{name} compiler missing: {programs[name]}")
with tempfile.TemporaryDirectory(prefix="noqeri-compile-perf-") as tmp:
    source=Path(tmp)/"workload.noqeri"
    output=Path(tmp)/"workload.s"
    lines=[
        "module benchmark.compiler",
        "function seed(value: int): int { return value + 1 }",
    ]
    for i in range(500):
        lines.append(f"function f{i}(value: int): int {{ return value + {i} }}")
    lines+=["let result: int = f499(seed(1))","print(result)"]
    source.write_text("\n".join(lines)+"\n")

    def run(label):
        start=time.perf_counter_ns()
        done=subprocess.run(
            [str(programs[label]),"build",str(source),str(output),"-O3"],
            cwd=ROOT,capture_output=True,text=True,timeout=120,check=True
        )
        elapsed=(time.perf_counter_ns()-start)/1e6
        if not output.is_file():
            raise RuntimeError(f"{label}: no emitted assembly")
        assembly=output.read_text()
        if "noqeri_entry" not in assembly or ".intel_syntax" not in assembly:
            raise RuntimeError(f"{label}: malformed native assembly output")
        return elapsed
    for label in programs:
        run(label)
    for _ in range(3):
        for label in programs:
            run(label)
    samples={k:[] for k in programs}
    order=list(programs)
    for i in range(args.samples):
        for label in (order if i%2==0 else order[::-1]):
            samples[label].append(run(label))

def summary(raw):
    return {
        "raw_ms":raw,"median_ms":statistics.median(raw),
        "p95_ms":sorted(raw)[math.ceil(len(raw)*0.95)-1],
    }
measurements={k:summary(v) for k,v in samples.items()}
report={
    "execution":"local-stage0-compiler-build",
    "machine":platform.platform(),
    "samples":args.samples,"warmups":3,
    "fixture":"501 functions, -O3 native assembly generation",
    "binaries":{
        k:{"path":str(v),"sha256":hashlib.sha256(v.read_bytes()).hexdigest()}
        for k,v in programs.items()
    },
    "measurements":measurements,
    "note":"Compile throughput only; does not assert native program speed.",
}
if "baseline" in measurements:
    report["candidate_compile_speedup"]=(
        measurements["baseline"]["median_ms"]/
        measurements["candidate"]["median_ms"]
    )
output_path=Path(args.output)
if not output_path.is_absolute():
    output_path=ROOT/output_path
output_path.parent.mkdir(parents=True,exist_ok=True)
output_path.write_text(json.dumps(report,indent=2)+"\n")
print(json.dumps(report,indent=2))
