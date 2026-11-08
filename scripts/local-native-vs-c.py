#!/usr/bin/env python3
"""Local-only C vs Noqeri standalone native AOT comparison.

Build Noqeri Stage-0 first and run on Linux x86-64:
  python3 scripts/local-native-vs-c.py --compiler build-local-perf/noqeri
Raw same-machine timing samples and validated outputs are written to JSON.
No GitHub Actions, Noqeri interpreter, Go, or remote services involved.
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
SOURCES = {
    "arithmetic_checksum": (
        "Benchmarks/workloads/arithmetic_checksum.noqeri",
        "12488332508630596960\n",
    ),
    "modulo": ("Benchmarks/workloads/modulo_ab.noqeri", "239999951\n"),
    "matrix": ("build-local-perf/matrix-comparison.noqeri", "29971\n28448\n"),
    "sorting_stable": ("Benchmarks/workloads/sorting.noqeri", "64\n65032\n"),
    "sorting_reshuffled": (
        "Benchmarks/workloads/sorting_reshuffled.noqeri", "13104300\n"
    ),
}

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", default="build-local-perf/noqeri")
parser.add_argument("--output", default="build-local-perf/c-vs-noqeri.json")
parser.add_argument("--samples", type=int, default=25)
args = parser.parse_args()
if args.samples < 15:
    parser.error("require at least 15 samples")
if platform.system() != "Linux" or platform.machine() not in ("x86_64", "AMD64"):
    parser.error("Noqeri standalone native AOT qualification requires Linux x86-64")
build = ROOT / "build-local-perf"
build.mkdir(parents=True, exist_ok=True)
matrix_src = (ROOT / "Benchmarks/workloads/matrix.noqeri").read_text()
old = "while pass < 20 as usize"
if matrix_src.count(old) != 1:
    raise RuntimeError("matrix fixture no longer matches pinned comparable work")
(build / "matrix-comparison.noqeri").write_text(
    matrix_src.replace(old, "while pass < 200 as usize")
)

def run(command, *, cwd=ROOT):
    complete = subprocess.run(
        [str(x) for x in command], cwd=cwd, capture_output=True,
        check=True, timeout=120,
    )
    return complete.stdout.decode()

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

c_binary = build / "bench-c"
run(["cc", "-O3", "-std=c17", "-o", c_binary,
     ROOT / "Benchmarks/comparison/workloads.c"])
compiler = Path(args.compiler)
if not compiler.is_absolute():
    compiler = (ROOT / compiler).resolve()
if not compiler.is_file():
    parser.error(f"Noqeri compiler not built: {compiler}")

cpu = "unknown"
cpuinfo = Path("/proc/cpuinfo")
if cpuinfo.exists():
    for line in cpuinfo.read_text(errors="replace").splitlines():
        if line.startswith("model name"):
            cpu = line.split(":", 1)[1].strip()
            break

out = {}
for workload, (source, expected) in SOURCES.items():
    nq_binary = build / ("bench-noqeri-" + workload)
    run(["node", "scripts/native-build.mjs", source, nq_binary,
         f"--compiler={compiler}", "--opt=-O3"])
    programs = {"C": [c_binary, workload], "Noqeri": [nq_binary]}
    sample_ms = {k: [] for k in programs}
    for k, command in programs.items():
        if run(command) != expected:
            raise RuntimeError(f"{workload}: {k} output mismatch")
    for _ in range(3):
        for command in programs.values():
            run(command)
    for i in range(args.samples):
        for k in (("C", "Noqeri") if i % 2 == 0 else ("Noqeri", "C")):
            start = time.perf_counter_ns()
            actual = run(programs[k])
            elapsed = (time.perf_counter_ns() - start) / 1e6
            if actual != expected:
                raise RuntimeError(f"{workload}: {k} changed output")
            sample_ms[k].append(elapsed)
    summaries = {}
    for k, values in sample_ms.items():
        sorted_values = sorted(values)
        summaries[k] = {
            "raw_ms": values,
            "median_ms": statistics.median(values),
            "p95_ms": sorted_values[math.ceil(len(values)*0.95)-1],
            "binary_sha256": sha(programs[k][0]),
            "binary_bytes": programs[k][0].stat().st_size,
        }
    out[workload] = {
        "expected_stdout": expected,
        "implementations": summaries,
        "c_over_noqeri_ratio": (
            summaries["C"]["median_ms"] / summaries["Noqeri"]["median_ms"]
        ),
    }

def version(command):
    return run(command).strip().splitlines()[0]

report = {
    "schema": 1,
    "machine": platform.platform(),
    "cpu": cpu,
    "compiler": str(compiler),
    "compiler_sha256": sha(compiler),
    "flags": {"C": "-O3 -std=c17", "Noqeri": "-O3 native-aot"},
    "cc_version": version(["cc", "--version"]),
    "node_version": version(["node", "--version"]),
    "warmups": 3,
    "samples": args.samples,
    "measurement": "wall time including process startup, compiler build excluded",
    "self_hosted_compiler": False,
    "results": out,
    "limitations": (
        "Five deterministic kernels on one Linux x86-64 host; not "
        "general C performance parity. This run is local, not CI."
    ),
}
output = Path(args.output)
if not output.is_absolute():
    output = ROOT / output
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
