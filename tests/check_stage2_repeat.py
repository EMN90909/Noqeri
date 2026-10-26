#!/usr/bin/env python3
"""Assert Stage-2 repeat parity through the real bootstrap compiler interpreter."""
import subprocess
import sys
from pathlib import Path

compiler=Path(sys.argv[1]).resolve()
sample=Path(sys.argv[2]).resolve()
check=subprocess.run([str(compiler),"check",str(sample)],capture_output=True,
                     text=True,timeout=40)
assert check.returncode==0,(check.stdout,check.stderr)
run=subprocess.run([str(compiler),"run",str(sample)],capture_output=True,
                   text=True,timeout=120)
assert run.returncode==0,(run.stdout,run.stderr)
assert run.stdout.split()==["0","0","0"],(run.stdout,run.stderr)
print("PASS: Stage-2 repeat parser, integer-count semantics, NIR lowering, and rejection cases")
