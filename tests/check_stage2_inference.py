#!/usr/bin/env python3
"""Actual self-hosted Stage-2 inference semantic checks executed in Stage-0 VM."""
import subprocess,sys
from pathlib import Path
compiler=Path(sys.argv[1]).resolve()
source=Path("tests/stage2_return_inference.noqeri").resolve()
for action in ("check","run"):
    cmd=[str(compiler),action,str(source),"--unsafe"]
    result=subprocess.run(cmd,capture_output=True,text=True,timeout=180)
    assert result.returncode==0,(action,result.returncode,result.stdout,result.stderr)
    if action=="run":
        values=result.stdout.strip().splitlines()
        assert values==["0"]*9,(values,result.stderr)
print("PASS: 9 self-hosted inferred-return positive/negative cases")
