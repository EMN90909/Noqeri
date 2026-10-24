#!/usr/bin/env python3
"""Stage-0 native ELF trap parity between O0 and O3 (Linux x86-64).

A 64-bit signed division/remainder with INT64_MIN and -1 traps on x86-64;
optimizers must not replace that trap with wrapped arithmetic or a constant.
Division by zero must also remain a trap for both signed and unsigned operands.
"""
import argparse
import resource
import signal
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "build-typed" / "division-trap-cases"
WORK.mkdir(parents=True, exist_ok=True)

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", default="build-typed/noqeri")
args = parser.parse_args()

cases = {
    "signed_overflow_div": (
        "i64", "9223372036854775808 as i64", "-1 as i64", "/"
    ),
    "signed_overflow_mod": (
        "i64", "9223372036854775808 as i64", "-1 as i64", "%"
    ),
    "signed_zero_divisor": ("i64", "97 as i64", "0 as i64", "/"),
    "unsigned_zero_divisor": ("u64", "97 as u64", "0 as u64", "%"),
}


def disable_core_dump():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


for name, (kind, numerator, denominator, operation) in cases.items():
    source = WORK / f"{name}.noqeri"
    source.write_text(
        f"let numerator: {kind} = {numerator}\n"
        f"let denominator: {kind} = {denominator}\n"
        f"print(numerator {operation} denominator)\n"
    )
    for opt in ("-O0", "-O3"):
        output = WORK / f"{name}-{opt[1:]}"
        subprocess.run(
            ["node", "scripts/native-build.mjs", str(source), str(output),
             f"--compiler={args.compiler}", f"--opt={opt}"],
            cwd=ROOT, check=True, capture_output=True, text=True
        )
        actual = subprocess.run(
            [str(output)], cwd=ROOT, capture_output=True,
            preexec_fn=disable_core_dump, check=False
        )
        if actual.returncode != -signal.SIGFPE:
            raise SystemExit(
                f"{name} {opt}: expected SIGFPE, got return "
                f"{actual.returncode}; stdout={actual.stdout!r}, "
                f"stderr={actual.stderr!r}"
            )
        print(f"{name} {opt}: SIGFPE preserved")
