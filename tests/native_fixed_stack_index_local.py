#!/usr/bin/env python3
"""Local x86-64 native execution of fixed stack array addressing.

Assemble the emitted code, call the three exported array functions for
boundary-valid indices, and ensure an out-of-bounds call still traps.
No GitHub Actions or remote execution is required.
"""
import argparse
import resource
import signal
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--emitter", default="build-local-perf/noqeri-fixed-stack-index-test")
args = parser.parse_args()
emitter = Path(args.emitter)
if not emitter.is_absolute():
    emitter = (ROOT / emitter).resolve()
if not emitter.is_file():
    parser.error(f"build the native emitter first: {emitter}")

def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0,0))

with tempfile.TemporaryDirectory(prefix="noqeri-array-aot-") as tmp:
    root = Path(tmp)
    assembly = root / "arrays.s"
    obj = root / "arrays.o"
    subprocess.run([str(emitter),str(assembly)],cwd=ROOT,check=True,timeout=60)
    subprocess.run(["cc","-c",str(assembly),"-o",str(obj)],
                   cwd=ROOT,check=True,timeout=60)
    for name, expected, source in (
        ("valid", 0, "tests/cpp/native_fixed_stack_index_host.c"),
        ("invalid", -signal.SIGILL, "tests/cpp/native_fixed_stack_index_oob.c"),
    ):
        executable=root / name
        subprocess.run([
            "cc","-std=c11","-O2",str(ROOT / source),str(obj),
            "-o",str(executable),
        ],cwd=ROOT,check=True,timeout=60)
        completed=subprocess.run(
            [str(executable)],capture_output=True,text=True,
            cwd=ROOT,preexec_fn=no_core,timeout=60,
        )
        if completed.returncode!=expected:
            raise RuntimeError(
                f"{name} produced code {completed.returncode} not {expected}; "
                f"stdout={completed.stdout!r} stderr={completed.stderr!r}"
            )
        if name=="valid" and completed.stdout!="indexed-array-ok\n":
            raise RuntimeError(f"valid array output mismatch: {completed.stdout!r}")
        print(f"{name}: expected native behavior validated")
