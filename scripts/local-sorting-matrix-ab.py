#!/usr/bin/env python3
"""Local sorting/matrix/arithmetic performance A/B: Noqeri vs Go vs C.

Build a RELEASE Stage-0 Noqeri compiler first. Example:
    cmake -S . -B build-local-perf -DCMAKE_BUILD_TYPE=Release
    cmake --build build-local-perf --target noqeri --parallel 2
    python3 scripts/local-sorting-matrix-ab.py --candidate build-local-perf/noqeri

For a same-host Noqeri compiler regression:
    python3 scripts/local-sorting-matrix-ab.py \
        --baseline /path/to/older/noqeri --candidate build-local-perf/noqeri

No GitHub Actions. Exactly the same reshuffled insertion-sort work and 200
16x16 matrix passes as Benchmarks/comparison/workloads.go/.c. Outputs must
match before warmups and after every timed run. Raw samples are retained.
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
WORKLOADS = {
    "arithmetic_checksum": {
        "source": ROOT / "Benchmarks/workloads/arithmetic_checksum.noqeri",
        "expected": "12488332508630596960\n",
    },
    "sorting_reshuffled": {
        "source": ROOT / "Benchmarks/workloads/sorting_reshuffled.noqeri",
        "expected": "13104300\n",
    },
    "matrix": {
        "source": ROOT / "build-local-perf/matrix-comparison.noqeri",
        "expected": "29971\n28448\n",
    },
}
parser = argparse.ArgumentParser()
parser.add_argument("--candidate", default="build-local-perf/noqeri")
parser.add_argument("--baseline", help="optional older Noqeri compiler binary")
parser.add_argument("--samples", type=int, default=25)
parser.add_argument("--output", default="build-local-perf/sorting-matrix-ab.json")
args = parser.parse_args()
if args.samples < 15:
    parser.error("at least 15 samples are required")
if platform.system() != "Linux" or platform.machine() not in ("x86_64", "AMD64"):
    parser.error("native Noqeri AOT comparison requires Linux x86-64")

def absolute(value):
    path = Path(value)
    return (path if path.is_absolute() else ROOT / path).resolve()

candidate = absolute(args.candidate)
baseline = absolute(args.baseline) if args.baseline else None
for binary in (candidate, baseline):
    if binary is not None and not binary.is_file():
        parser.error(f"Noqeri compiler binary not found: {binary}")
output = absolute(args.output)
output.parent.mkdir(parents=True, exist_ok=True)
matrix = (ROOT / "Benchmarks/workloads/matrix.noqeri").read_text()
old = "while pass < 20 as usize"
if matrix.count(old) != 1:
    raise RuntimeError("The matrix workload no longer has its pinned pass count")
WORKLOADS["matrix"]["source"].write_text(
    matrix.replace(old, "while pass < 200 as usize")
)

def call(command, *, env=None):
    return subprocess.run(
        [str(x) for x in command], cwd=ROOT, check=True,
        capture_output=True, timeout=180, env=env,
    )

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

c_binary = output.parent / "sorting-matrix-c"
go_binary = output.parent / "sorting-matrix-go"
call(["cc", "-std=c17", "-O3", ROOT / "Benchmarks/comparison/workloads.c",
      "-o", c_binary])
call(["go", "build", "-trimpath", "-o", go_binary,
      ROOT / "Benchmarks/comparison/workloads.go"])
compilers = {"candidate": candidate}
if baseline:
    compilers["baseline"] = baseline

build_records = {}
for work, cfg in WORKLOADS.items():
    compiled = {"C": [str(c_binary), work], "Go": [str(go_binary), work]}
    for name, compiler in compilers.items():
        dest = output.parent / f"{work}-noqeri-{name}"
        compile_command = [
            "node", "scripts/native-build.mjs", cfg["source"], dest,
            f"--compiler={compiler}", "--opt=-O3",
        ]
        start = time.perf_counter_ns()
        call(compile_command)
        build_records[f"{work}:{name}"] = {
            "compiler_sha256": digest(compiler),
            "build_ms": (time.perf_counter_ns()-start)/1e6,
            "binary_sha256": digest(dest),
            "binary_bytes": dest.stat().st_size,
            "provenance": str(dest) + ".native.json",
        }
        compiled["Noqeri_" + name] = [str(dest)]

    expected = cfg["expected"].encode()
    def run(label):
        started = time.perf_counter_ns()
        result = call(compiled[label])
        elapsed = (time.perf_counter_ns()-started)/1e6
        if result.stdout != expected:
            raise RuntimeError(
                f"{work}: {label} wrong result: {result.stdout!r} != {expected!r}"
            )
        return elapsed
    for label in compiled:
        run(label)  # pre-qualification
    for _ in range(3):
        for label in compiled:
            run(label)
    raw = {label: [] for label in compiled}
    labels = tuple(compiled.keys())
    for i in range(args.samples):
        order = labels[i % len(labels):] + labels[:i % len(labels)]
        for label in order:
            raw[label].append(run(label))
    summarized = {}
    for label, values in raw.items():
        ordered = sorted(values)
        summarized[label] = {
            "raw_ms": values,
            "median_ms": statistics.median(values),
            "p95_ms": ordered[math.ceil(len(ordered)*.95)-1],
        }
    cfg["measurements"] = summarized
    cfg["noqeri_vs_go"] = (
        summarized["Noqeri_candidate"]["median_ms"] /
        summarized["Go"]["median_ms"]
    )
    if baseline:
        cfg["candidate_speedup"] = (
            summarized["Noqeri_baseline"]["median_ms"] /
            summarized["Noqeri_candidate"]["median_ms"]
        )

cpu = "unknown"
cpuinfo = Path("/proc/cpuinfo")
if cpuinfo.exists():
    for line in cpuinfo.read_text(errors="replace").splitlines():
        if line.startswith("model name"):
            cpu = line.split(":",1)[1].strip()
            break
report = {
    "schema": 1,
    "execution": "same-host native AOT, standalone binaries",
    "host": platform.platform(),
    "machine": platform.machine(),
    "cpu": cpu,
    "compilers": {
        name: {"path": str(binary), "sha256": digest(binary)}
        for name, binary in compilers.items()
    },
    "cc_version": call(["cc", "--version"]).stdout.decode().splitlines()[0],
    "go_version": call(["go", "version"]).stdout.decode().strip(),
    "node_version": call(["node", "--version"]).stdout.decode().strip(),
    "build_records": build_records,
    "samples": args.samples,
    "warmups": 3,
    "measurement": "wall-clock process launch plus workload runtime",
    "workloads": {
        name: {
            "source": str(cfg["source"].relative_to(ROOT)),
            "expected_stdout": cfg["expected"],
            "measurements": cfg["measurements"],
            "noqeri_over_go_ratio": cfg["noqeri_vs_go"],
            **({"candidate_speedup_over_baseline": cfg["candidate_speedup"]}
               if baseline else {}),
        }
        for name, cfg in WORKLOADS.items()
    },
    "limitations": (
        "Only these two deterministic kernels, on this host. "
        "No synthetic performance claims, no GC or general compiler parity."
    ),
}
output.write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
