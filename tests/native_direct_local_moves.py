#!/usr/bin/env python3
"""Regression for native AOT local-copy lowering on Linux x86-64.

Checks standalone execution at -O0 and -O3, not interpreter semantics.
An assembly shape check at -O0 guards against returning to two-hop RAX
copies for register-promoted named locals.
"""
from pathlib import Path
import platform
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tests/native_direct_local_moves.noqeri"
EXPECTED = "499500\n1000\n7\n499507\n"


def main() -> int:
    if sys.platform != "linux" or platform.machine() not in ("x86_64", "AMD64"):
        print("requires Linux x86-64 native AOT", file=sys.stderr)
        return 77
    compiler = Path(sys.argv[1] if len(sys.argv) > 1 else "build/noqeri")
    compiler = compiler if compiler.is_absolute() else (ROOT / compiler).resolve()
    if not compiler.is_file():
        raise RuntimeError(f"compiler not found: {compiler}")

    folder = ROOT / "build" / "native-direct-local-moves"
    folder.mkdir(parents=True, exist_ok=True)
    for level in ("-O0", "-O3"):
        binary = folder / ("direct-" + level[2:])
        subprocess.run(
            ["node", "scripts/native-build.mjs", str(SOURCE), str(binary),
             f"--compiler={compiler}", f"--opt={level}", "--keep-asm"],
            check=True, cwd=ROOT,
        )
        actual = subprocess.run(
            [str(binary)], check=True, cwd=ROOT, capture_output=True, text=True
        ).stdout
        if actual != EXPECTED:
            raise AssertionError(
                f"{level}: wrong native output: expected {EXPECTED!r}, got {actual!r}"
            )
        if level == "-O0":
            assembly = Path(str(binary) + ".s").read_text()
            # Only count a direct transfer from the promoted RBX local into
            # an allocated register or a spill slot. Avoid relying on a
            # particular linear-scan assignment order.
            direct = re.search(
                r"(?m)^\s*mov (?:r1[2-5]|QWORD PTR \[rbp-\d+\]), rbx$",
                assembly,
            )
            if not direct:
                raise AssertionError(
                    "no direct copy from promoted RBX local: "
                    "native Load/Store may have regressed to RAX hopping"
                )
        print(f"{level}: standalone native results and local move lowering OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
