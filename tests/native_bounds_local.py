#!/usr/bin/env python3
"""Verify native array bounds and scalar SSA/Phi output at O0 and O3.

Run against a locally built Stage-0 compiler on Linux x86-64.
No GitHub Actions. UB2 bounds traps must remain traps after optimization.
"""
import argparse
import resource
import signal
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--compiler", default="build-local-perf/noqeri")
args = parser.parse_args()
compiler = Path(args.compiler)
if not compiler.is_absolute():
    compiler = (ROOT / compiler).resolve()

def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

cases = {
    "native_bounds_guard": ("0\n21\n", 0),
    "native_bounds_oob_trap": ("", -signal.SIGILL),
    "native_bounds_mutation_trap": ("", -signal.SIGILL),
    "native_dynamic_unsigned_bounds": ("21\n", 0),
    "native_dynamic_unsigned_oob": ("", -signal.SIGILL),
    "native_phi_diamond": ("37\n81\n", 0),
    "native_ssa_general": ("1\n3\n16\n20192\n133352\n0\n0\n78\n1188\n", 0),
}

with tempfile.TemporaryDirectory(prefix="noqeri-bounds-") as tmp:
    for stem, (expected, exitcode) in cases.items():
        for level in ("-O0", "-O3"):
            binary = Path(tmp) / f"{stem}-{level[1:]}"
            subprocess.run([
                "node", "scripts/native-build.mjs",
                str(ROOT / "tests" / (stem + ".noqeri")), str(binary),
                f"--compiler={compiler}", f"--opt={level}",
            ], cwd=ROOT, check=True)
            result = subprocess.run(
                [str(binary)], cwd=ROOT, capture_output=True, text=True,
                preexec_fn=no_core, check=False,
            )
            if result.returncode != exitcode or result.stdout != expected:
                raise RuntimeError(
                    f"{stem} {level}: code={result.returncode}, "
                    f"stdout={result.stdout!r}; expected {exitcode}, {expected!r}"
                )
            print(f"{stem} {level}: output/trap parity OK")
