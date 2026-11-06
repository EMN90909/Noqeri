#!/usr/bin/env python3
"""Local native O0/O3 regression for the new in-place i64 sort API."""
import argparse
import subprocess
import tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument("--compiler",default="build-local-perf/noqeri")
args=parser.parse_args()
compiler=Path(args.compiler)
if not compiler.is_absolute():
    compiler=(ROOT/compiler).resolve()
if not compiler.is_file():
    parser.error(f"compiler missing: {compiler}")
expected="1\n-50\n50\n50\n-50\n47\n1\n8\n"
with tempfile.TemporaryDirectory(prefix="noqeri-sort-local-") as temp:
    for opt in ("-O0","-O3"):
        binary=Path(temp)/("heap-"+opt[1:])
        subprocess.run([
            "node","scripts/native-build.mjs",
            "tests/native_sort_heap.noqeri",str(binary),
            f"--compiler={compiler}",f"--opt={opt}",
        ],cwd=ROOT,check=True,timeout=180)
        result=subprocess.run(
            [str(binary)],cwd=ROOT,check=True,capture_output=True,
            text=True,timeout=60,
        )
        if result.stdout!=expected:
            raise RuntimeError(
                f"{opt} heap-sort output mismatch: "
                f"expected {expected!r}, got {result.stdout!r}"
            )
        print(f"{opt}: heap-sort O(n log n), descending, duplicates and short-list PASS")
