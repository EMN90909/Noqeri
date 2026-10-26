#!/usr/bin/env python3
"""Deterministic cross-runtime differential corpus for bounded Stage-2 language.

Not universal language equivalence. Independently compute expected outcomes in
Python; compile and execute Noqeri source with Stage-0 and rebuilt Stage-2.
A selected subset also executes as genuine Linux x86-64 native AOT.
"""
from __future__ import annotations
import pathlib
import random
import subprocess
import sys


def execute(args: list[str], expected: str | None = None) -> None:
    proc = subprocess.run(args, text=True, capture_output=True, timeout=45, check=False)
    if proc.returncode or (expected is not None and proc.stdout.strip() != expected):
        raise AssertionError(
            f"command {args!r} failed: rc={proc.returncode}; "
            f"expected={expected!r}; stdout={proc.stdout[-3000:]!r}; "
            f"stderr={proc.stderr[-3000:]!r}"
        )


def fixture(index: int, rng: random.Random) -> str:
    values = [rng.randint(-8, 14) for _ in range(4)]
    skip = rng.randint(-4, 4)
    times = rng.randint(-3, 6)
    increment = rng.randint(-5, 8)
    break_at = rng.randint(2, 8)
    odd_total = sum(x for x in values if x != skip and x % 2 != 0)
    total = odd_total + max(0, times) * increment
    nested = 0
    for _ in range(2):
        for _ in range(3):
            nested += 1
            if nested % 3 == 0:
                continue
            if nested >= break_at:
                break
    return f"""// Seeded Stage-2 property fixture #{index}: no host capabilities.
function verify(): int {{
    let values: [int; 4] = [{", ".join(map(str, values))}]
    let sum: int = 0
    for item in values {{
        if item == {skip} {{ continue }}
        if item % 2 != 0 {{ sum = sum + item }}
    }}
    repeat {times} {{
        sum = sum + {increment}
    }}
    if sum != {total} {{ return 1 }}
    let nested: int = 0
    repeat 2 {{
        repeat 3 {{
            nested = nested + 1
            if nested % 3 == 0 {{ continue }}
            if nested >= {break_at} {{ break }}
        }}
    }}
    if nested != {nested} {{ return 2 }}
    return 0
}}
print(verify())
"""


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit("usage: script STAGE0 NQRI STAGE2_MODULE OUT_DIR")
    stage0, nqri, stage2, root = sys.argv[1:]
    folder = pathlib.Path(root)
    folder.mkdir(parents=True, exist_ok=True)
    rng = random.Random(20261026)
    for index in range(20):
        source = folder / f"case-{index:02}.noqeri"
        bytecode = folder / f"case-{index:02}.nqri"
        source.write_text(fixture(index, rng), encoding="utf-8")
        execute([stage0, "check", str(source)])
        execute([stage0, "run", str(source)], expected="0")
        execute([nqri, "compile", stage2, str(source), str(bytecode), "3"])
        execute([nqri, str(bytecode), "verify"], expected="0")
        if index < 3:
            native = folder / f"native-{index:02}"
            execute([
                "node", "scripts/native-build.mjs", str(source), str(native),
                f"--compiler={stage0}",
            ])
            execute([str(native)], expected="0")
        print(f"PASS property {index:02}")
    print("PASS 20 Stage-0 / rebuilt Stage-2 differential fixtures; 3 native AOT")


if __name__ == "__main__":
    main()
