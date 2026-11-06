#!/usr/bin/env python3
"""Manual native AOT regression for branch-free u64 magic division.

No GitHub Actions involved. Run on Linux x86-64 after building Stage-0:
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
  cmake --build build --target noqeri --parallel 2
  python3 tests/native_branchfree_division.py --compiler=build/noqeri

The generated program tests *dynamic* unsigned/signed operands under both
-O0 and -O3, checks printed results against Python integer arithmetic, and
inspects emitted assembly for the magic-multiply path. It is a correctness
test, not a C-vs-Noqeri speed claim.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
MASK = (1 << 64) - 1
ROUNDS = 2048
UNSIGNED_DIVISORS = [3, 7, 97, 1000000007, (1 << 63) + 1, MASK]
SIGNED_DIVISORS = [3, 7, 97, -3, -7, -97, -(1 << 63)]


def signed64(n):
    n &= MASK
    return n if n < (1 << 63) else n - (1 << 64)


def truncdiv(n, d):
    q = abs(n) // abs(d)
    return -q if (n < 0) != (d < 0) else q


parser = argparse.ArgumentParser()
parser.add_argument("--compiler", default="build/noqeri")
args = parser.parse_args()
src = [
    "let i: u64 = 0 as u64",
    "let unsigned_sum: u64 = 0 as u64",
    "let signed_sum: i64 = 0 as i64",
    f"while i < {ROUNDS} as u64 {{",
    "    let low: u64 = i * 7919 as u64 + 104729 as u64",
    "    let high: u64 = 18446744073709551615 as u64 - i",
    f"    let value: i64 = (i as i64) - {ROUNDS // 2} as i64",
]
for divisor in UNSIGNED_DIVISORS:
    for var in ("low", "high"):
        src.append(f"    unsigned_sum = unsigned_sum + ({var} / {divisor} as u64)")
        src.append(f"    unsigned_sum = unsigned_sum + ({var} % {divisor} as u64)")
for divisor in SIGNED_DIVISORS:
    literal = f"{divisor} as i64" if divisor != -(1 << 63) else "9223372036854775808 as i64"
    src.append(f"    signed_sum = signed_sum + (value / {literal})")
    src.append(f"    signed_sum = signed_sum + (value % {literal})")
src.extend(["    i = i + 1 as u64", "}", "print(unsigned_sum)", "print(signed_sum)"])

unsigned_expected = 0
signed_expected = 0
for i in range(ROUNDS):
    for n in ((i * 7919 + 104729) & MASK, MASK - i):
        for d in UNSIGNED_DIVISORS:
            unsigned_expected = (unsigned_expected + n // d + n % d) & MASK
    signed_n = i - ROUNDS // 2
    for d in SIGNED_DIVISORS:
        q = truncdiv(signed_n, d)
        signed_expected += q + (signed_n - q * d)
expected = [str(signed64(unsigned_expected)), str(signed64(signed_expected))]

with tempfile.TemporaryDirectory(prefix="noqeri-magic-") as directory:
    source = Path(directory) / "branchfree.noqeri"
    source.write_text("\n".join(src) + "\n")
    for level in ("-O0", "-O3"):
        executable = Path(directory) / f"branchfree-{level[2:]}"
        subprocess.run(
            ["node", "scripts/native-build.mjs", str(source), str(executable),
             f"--compiler={args.compiler}", f"--opt={level}", "--keep-asm"],
            cwd=ROOT, check=True,
        )
        assembly = Path(str(executable) + ".s").read_text()
        if "mul r10" not in assembly or "div_done" in assembly:
            raise SystemExit(f"{level}: expected branch-free constant-divisor lowering")
        actual = subprocess.run(
            [str(executable)], cwd=ROOT, check=True,
            capture_output=True, text=True,
        ).stdout.strip().splitlines()
        if actual != expected:
            raise SystemExit(f"{level}: got {actual!r}, expected {expected!r}")
        print(f"{level}: native division and remainder correct ({ROUNDS} iterations)")
