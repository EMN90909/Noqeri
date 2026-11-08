#!/usr/bin/env python3
"""Compile and run sorting/checksum correctness fixtures at O0 and O3.

This is a functional regression test, not a benchmark or timing harness.
Runs the current Stage-0 AOT compiler locally. No GitHub Actions.
"""
import argparse
import subprocess
import tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument("--compiler",default="build/noqeri")
args=parser.parse_args()
compiler=Path(args.compiler)
if not compiler.is_absolute():
    compiler=(ROOT/compiler).resolve()
if not compiler.is_file():
    parser.error(f"Stage-0 compiler missing: {compiler}")
with tempfile.TemporaryDirectory(prefix="noqeri-sort-checksum-test-") as temp:
    for opt in ("-O0","-O3"):
        binary=Path(temp)/("sort-checksum-"+opt[1:])
        subprocess.run([
            "node","scripts/native-build.mjs",
            "tests/native_sort_arithmetic_regression.noqeri",str(binary),
            f"--compiler={compiler}",f"--opt={opt}",
        ],cwd=ROOT,check=True,timeout=180)
        completed=subprocess.run([str(binary)],cwd=ROOT,
                                 check=True,capture_output=True,text=True,timeout=120)
        if completed.stdout!="PASS\n":
            raise AssertionError(f"{opt} correctness regression: {completed.stdout!r}, stderr={completed.stderr!r}")
        print(f"{opt}: in-place/stable sorting, exact arithmetic, and checksums PASS")
    # Retain the original general-sort compatibility output as an independent
    # golden fixture for callers that already use nqSortI64.
    golden="1\\n-50\\n50\\n50\\n-50\\n47\\n1\\n8\\n1\\n-8\\n8\\n11\\n"
    for opt in ("-O0","-O3"):
        binary=Path(temp)/("sort-compat-"+opt[1:])
        subprocess.run([
            "node","scripts/native-build.mjs",
            "tests/native_sort_heap.noqeri",str(binary),
            f"--compiler={compiler}",f"--opt={opt}",
        ],cwd=ROOT,check=True,timeout=180)
        completed=subprocess.run([str(binary)],cwd=ROOT,
                                 check=True,capture_output=True,text=True,timeout=120)
        if completed.stdout!=golden:
            raise AssertionError(f"{opt} existing sort API output changed: {completed.stdout!r}")
        print(f"{opt}: existing sort API golden output PASS")
