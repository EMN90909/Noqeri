#!/usr/bin/env python3
"""Native division correctness matrix, not a speed benchmark.

Run against the actual Stage-0 ELF backend, exercising nonconstant dividends
and multiple constant signed/unsigned divisors. Compare with Python's exact
integer arithmetic, adjusted to C/Noqeri truncation semantics and the native
print ABI's signed-i64 decimal representation.
"""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "build-native"
WORK.mkdir(parents=True, exist_ok=True)
SOURCE = WORK / "division_property.noqeri"
OUTPUT = WORK / "division_property"
MASK = (1 << 64) - 1


def printed(value: int) -> str:
    value &= MASK
    return str(value if value < (1 << 63) else value - (1 << 64))


def truncdiv(n: int, d: int) -> int:
    value = abs(n) // abs(d)
    return -value if (n < 0) != (d < 0) else value


def as_literal(n: int, kind: str) -> str:
    if kind == "i64" and n == -(1 << 63):
        return "9223372036854775808 as i64"
    return f"{n} as {kind}"


unsigned_values = [0, 1, 2, 93, 97, (1 << 31) - 1,
                   (1 << 63) - 1, (1 << 63), MASK]
unsigned_divisors = [1, 2, 3, 7, 97, 65521,
                     2147483647, 1 << 31, 1 << 63, MASK]
signed_values = [-(1 << 63), -(1 << 63) + 1, -93, -7, -1,
                 0, 1, 93, (1 << 63) - 1]
signed_divisors = [1, 2, 3, 7, 97, 2147483647, -1, -2,
                   -7, -97, -(1 << 63)]
lines = []
expected = []
for values, divisors, kind in [
    (unsigned_values, unsigned_divisors, "u64"),
    (signed_values, signed_divisors, "i64"),
]:
    for i, n in enumerate(values):
        name = f"n_{kind}_{i}"
        lines.append(f"let {name}: {kind} = {as_literal(n, kind)}")
        for d in divisors:
            if n == -(1 << 63) and d == -1:
                # x86-64 IDIV overflow must trap; not a valid result here.
                continue
            lines.append(f"print({name} / {as_literal(d, kind)})")
            lines.append(f"print({name} % {as_literal(d, kind)})")
            quotient = (n // d if kind == "u64" else truncdiv(n, d))
            expected.extend([printed(quotient), printed(n - quotient * d)])
SOURCE.write_text("\n".join(lines) + "\n")
for level in ["-O0", "-O3"]:
    subprocess.run([
        "node", "scripts/native-build.mjs",
        str(SOURCE), str(OUTPUT), "--compiler=build-native/noqeri",
        f"--opt={level}",
    ], cwd=ROOT, check=True)
    data = subprocess.run([str(OUTPUT)], cwd=ROOT, check=True,
                          capture_output=True, text=True).stdout.strip().splitlines()
    if data != expected:
        pos = next((i for i, (a, b) in enumerate(zip(data, expected)) if a != b),
                   min(len(data), len(expected)))
        raise SystemExit(
            f"{level} incorrect division/remainder at output {pos}: "
            f"got={data[pos:pos+5]!r}, expected={expected[pos:pos+5]!r}"
        )
    print(f"{level}: {len(expected)} signed/unsigned quotient and remainder results matched")
