#!/usr/bin/env python3
"""Execute self-host Stage-2 parser/lowering through the Stage-0 compiler."""
import subprocess
import sys
from pathlib import Path

compiler = Path(sys.argv[1]).resolve()
source = Path(sys.argv[2]).resolve()
check = subprocess.run([str(compiler), "check", str(source), "--unsafe"],
                       capture_output=True, text=True, timeout=40)
if check.returncode != 0:
    raise AssertionError(f"Stage-2 harness did not compile: {check.stdout}\n{check.stderr}")
run = subprocess.run([str(compiler), "run", str(source), "--unsafe"],
                     capture_output=True, text=True, timeout=80)
if run.returncode != 0:
    raise AssertionError(f"Stage-2 harness crashed ({run.returncode}): {run.stdout}\n{run.stderr}")
expected = ["0"] * 6
if run.stdout.splitlines() != expected:
    raise AssertionError(f"Stage-2 runtime parity failed: expected {expected}, got {run.stdout!r}")
print("PASS: Stage-2 repeat, for-in, expression-function parser and NIR lowering")
