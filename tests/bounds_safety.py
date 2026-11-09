#!/usr/bin/env python3
"""Verification of memory-safe array bounds checking across Noqeri execution backends."""
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parent.parent
noqeri = root / "build" / "noqeri"
assert noqeri.is_file(), "noqeri binary not found"

source = root / "tests" / "bounds_dynamic_trap.noqeri"
assert source.is_file(), "test fixture not found"

print("[1/3] Testing Interpreter bounds checking...")
res_interp = subprocess.run([str(noqeri), "run", str(source)], capture_output=True, text=True)
assert res_interp.returncode != 0, f"Interpreter expected non-zero exit code, got {res_interp.returncode}"
assert "NQR-R4000: runtime error: index out of bounds" in (res_interp.stderr + res_interp.stdout), (
    f"Interpreter missing expected bounds error diagnostic: {res_interp.stderr} {res_interp.stdout}"
)
print("  Interpreter bounds check OK")

with tempfile.TemporaryDirectory() as tmpdir:
    print("[2/3] Testing LLVM backend bounds checking...")
    llvm_bin = pathlib.Path(tmpdir) / "bounds_llvm"
    build_llvm = subprocess.run(
        ["node", str(root / "scripts" / "native-build.mjs"), str(source), str(llvm_bin), "--backend=llvm"],
        capture_output=True, text=True
    )
    assert build_llvm.returncode == 0, f"LLVM build failed: {build_llvm.stderr}"
    res_llvm = subprocess.run([str(llvm_bin)], capture_output=True, text=True)
    assert res_llvm.returncode != 0, f"LLVM binary expected non-zero exit code, got {res_llvm.returncode}"
    assert "NQR-R4000: runtime error: index out of bounds" in (res_llvm.stderr + res_llvm.stdout), (
        f"LLVM binary missing expected bounds error diagnostic: {res_llvm.stderr} {res_llvm.stdout}"
    )
    print("  LLVM backend bounds check OK")

    print("[3/3] Testing Direct Native x86-64 backend bounds checking...")
    direct_bin = pathlib.Path(tmpdir) / "bounds_direct"
    build_direct = subprocess.run(
        ["node", str(root / "scripts" / "native-build.mjs"), str(source), str(direct_bin)],
        capture_output=True, text=True
    )
    assert build_direct.returncode == 0, f"Direct native build failed: {build_direct.stderr}"
    res_direct = subprocess.run([str(direct_bin)], capture_output=True, text=True)
    assert res_direct.returncode != 0, f"Direct native binary expected non-zero exit code, got {res_direct.returncode}"
    assert "NQR-R4000: runtime error: index out of bounds" in (res_direct.stderr + res_direct.stdout), (
        f"Direct native binary missing expected bounds error diagnostic: {res_direct.stderr} {res_direct.stdout}"
    )
    print("  Direct native backend bounds check OK")

print("PASS: Memory safety verified across interpreter, LLVM, and direct native backends.")
