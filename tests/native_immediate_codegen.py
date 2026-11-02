#!/usr/bin/env python3
"""Linux x86-64 native-AOT regression for immediate instructions.

Checks actual standalone program output at O0/O3, plus the O0 assembly shape.
Compilation time is deliberately not treated as benchmark runtime evidence.
"""
from pathlib import Path
import platform
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tests/native_immediate_codegen.noqeri"
EXPECTED = (
    "3989000\n"
    "1000\n"
    "true\n"
    "true\n"
    "true\n"
    "false\n"
    "-2147483665\n"
    "17\n"
)

def main() -> int:
    if sys.platform != "linux" or platform.machine() not in ("x86_64", "AMD64"):
        print("native immediate tests require Linux x86-64", file=sys.stderr)
        return 77
    compiler = Path(sys.argv[1] if len(sys.argv) > 1 else "build/noqeri")
    if not compiler.is_absolute():
        compiler = (ROOT / compiler).resolve()
    if not compiler.is_file():
        raise RuntimeError(f"missing compiler: {compiler}")

    output_dir = ROOT / "build/native-immediate-tests"
    output_dir.mkdir(parents=True, exist_ok=True)
    for optimization in ("-O0", "-O3"):
        output = output_dir / f"immediates-{optimization[2:]}"
        subprocess.run(
            ["node", "scripts/native-build.mjs", str(SOURCE), str(output),
             f"--compiler={compiler}", f"--opt={optimization}", "--keep-asm"],
            cwd=ROOT, check=True,
        )
        actual = subprocess.run(
            [str(output)], cwd=ROOT, check=True, capture_output=True, text=True
        ).stdout
        if actual != EXPECTED:
            raise AssertionError(
                f"{optimization} incorrect standalone output:\n"
                f"expected {EXPECTED!r}\nactual {actual!r}"
            )

        if optimization == "-O0":
            asm = Path(str(output) + ".s").read_text()
            # These are instruction-selection checks, not performance claims.
            for pattern in (
                r"\bshl rax, 3\b",
                r"\bsub rax, 7\b",
                r"\badd rax, 1\b",
                r"\bcmp rax, 1000\b",
                r"\bcmp rax, 2147483647\b",
                r"\bcmp rax, -2147483648\b",
            ):
                if not re.search(pattern, asm):
                    raise AssertionError(
                        f"native immediate instruction missing: {pattern}"
                    )
        print(f"{optimization}: standalone output and codegen OK")
    return 0

if __name__ == "__main__":
    sys.exit(main())
