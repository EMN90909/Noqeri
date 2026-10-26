#!/usr/bin/env python3
"""Typecheck Stage-2 parser/semantic/NIR sources; runtime parity not certified."""
import subprocess
import sys
from pathlib import Path
compiler=Path(sys.argv[1]).resolve()
sample=Path(sys.argv[2]).resolve()
check=subprocess.run([str(compiler),"check",str(sample),"--unsafe"],
                     capture_output=True,text=True,timeout=40)
assert check.returncode==0,(check.stdout,check.stderr)
print("PASS: Stage-2 repeat/for/typed-function source and NIR test harness typecheck")
print("NOTICE: Executing Stage-2 via the Stage-0 interpreter is not yet qualified; "
      "known memory corruption in the complex self-hosted parser test requires further investigation.")
