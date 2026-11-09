#!/usr/bin/env python3
import subprocess
import time
import statistics
import sys
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, "../.."))

WARMUPS = 2
BATCHES = 5

WORKLOADS = [
    {
        "name": "modulo",
        "description": "10,000,000 iteration modulo arithmetic",
        "expected_checksum": 4979997198,
        "commands": [
            ("C (gcc -O3)", [os.path.join(SCRIPT_DIR, "c_modulo")]),
            ("C++ (g++ -O3)", [os.path.join(SCRIPT_DIR, "cpp_modulo")]),
            ("Rust (rustc -O3)", [os.path.join(SCRIPT_DIR, "rust_modulo")]),
            ("Zig (ReleaseFast)", [os.path.join(SCRIPT_DIR, "zig_modulo")]),
            ("Golang (go build)", [os.path.join(SCRIPT_DIR, "go_modulo")]),
            ("JS (Number)", ["node", os.path.join(SCRIPT_DIR, "bench_modulo.js")]),
            ("JS (BigInt)", ["node", os.path.join(SCRIPT_DIR, "bench_modulo_bigint.js")]),
            ("Noqeri (LLVM AOT)", [os.path.join(SCRIPT_DIR, "noqeri_modulo_llvm")]),
            ("Noqeri (Direct Native)", [os.path.join(SCRIPT_DIR, "noqeri_modulo_native")]),
            ("Noqeri (VM/Interp)", [os.path.join(ROOT_DIR, "build/noqeri"), "run", os.path.join(SCRIPT_DIR, "bench_modulo.noqeri")]),
        ]
    },
    {
        "name": "matrix",
        "description": "256x256 matrix multiplication with full matrix checksum",
        "expected_checksum": 39802911680,
        "commands": [
            ("C (gcc -O3)", [os.path.join(SCRIPT_DIR, "c_matrix")]),
            ("C++ (g++ -O3)", [os.path.join(SCRIPT_DIR, "cpp_matrix")]),
            ("Rust (rustc -O3)", [os.path.join(SCRIPT_DIR, "rust_matrix")]),
            ("Zig (ReleaseFast)", [os.path.join(SCRIPT_DIR, "zig_matrix")]),
            ("Golang (go build)", [os.path.join(SCRIPT_DIR, "go_matrix")]),
            ("JS (Number)", ["node", os.path.join(SCRIPT_DIR, "bench_matrix.js")]),
            ("JS (BigInt)", ["node", os.path.join(SCRIPT_DIR, "bench_matrix_bigint.js")]),
            ("Noqeri (LLVM AOT)", [os.path.join(SCRIPT_DIR, "noqeri_matrix_llvm")]),
            ("Noqeri (Direct Native)", [os.path.join(SCRIPT_DIR, "noqeri_matrix_native")]),
        ]
    },
    {
        "name": "sorting",
        "description": "2,000-element insertion sort with full order check & weighted checksum",
        "expected_checksum": 132721688901,
        "commands": [
            ("C (gcc -O3)", [os.path.join(SCRIPT_DIR, "c_sort")]),
            ("C++ (g++ -O3)", [os.path.join(SCRIPT_DIR, "cpp_sort")]),
            ("Rust (rustc -O3)", [os.path.join(SCRIPT_DIR, "rust_sort")]),
            ("Zig (ReleaseFast)", [os.path.join(SCRIPT_DIR, "zig_sort")]),
            ("Golang (go build)", [os.path.join(SCRIPT_DIR, "go_sort")]),
            ("JS (Number)", ["node", os.path.join(SCRIPT_DIR, "bench_sort.js")]),
            ("JS (BigInt)", ["node", os.path.join(SCRIPT_DIR, "bench_sort_bigint.js")]),
            ("Noqeri (LLVM AOT)", [os.path.join(SCRIPT_DIR, "noqeri_sort_llvm")]),
            ("Noqeri (Direct Native)", [os.path.join(SCRIPT_DIR, "noqeri_sort_native")]),
        ]
    },
    {
        "name": "image",
        "description": "512x512 image buffer rendering & full buffer checksum",
        "expected_checksum": 2199023124480,
        "commands": [
            ("C (gcc -O3)", [os.path.join(SCRIPT_DIR, "c_image")]),
            ("C++ (g++ -O3)", [os.path.join(SCRIPT_DIR, "cpp_image")]),
            ("Rust (rustc -O3)", [os.path.join(SCRIPT_DIR, "rust_image")]),
            ("Zig (ReleaseFast)", [os.path.join(SCRIPT_DIR, "zig_image")]),
            ("Golang (go build)", [os.path.join(SCRIPT_DIR, "go_image")]),
            ("JS (Number)", ["node", os.path.join(SCRIPT_DIR, "bench_image.js")]),
            ("JS (BigInt)", ["node", os.path.join(SCRIPT_DIR, "bench_image_bigint.js")]),
            ("Noqeri (LLVM AOT)", [os.path.join(SCRIPT_DIR, "noqeri_image_llvm")]),
            ("Noqeri (Direct Native)", [os.path.join(SCRIPT_DIR, "noqeri_image_native")]),
        ]
    }
]

def run_cmd(cmd):
    t0 = time.perf_counter()
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    t1 = time.perf_counter()
    if res.returncode != 0:
        raise RuntimeError(f"Command failed ({res.returncode}): {' '.join(cmd)}\n{res.stderr}")
    out = res.stdout.strip()
    if not out:
        out = res.stderr.strip()
    
    # Try parsing internal elapsed if emitted, else use wall time
    # Binary outputs: "<checksum>\t<elapsed>" or "<Lang>\t<workload>\t<checksum>\t<elapsed>"
    parts = out.split()
    checksum = None
    elapsed = t1 - t0
    if len(parts) >= 2:
        try:
            # Check last token as float, second to last as int
            elapsed_val = float(parts[-1])
            checksum_val = int(parts[-2])
            checksum = checksum_val
            elapsed = elapsed_val
        except (ValueError, IndexError):
            try:
                checksum = int(parts[0])
                elapsed = float(parts[1])
            except (ValueError, IndexError):
                pass
    elif len(parts) == 1:
        try:
            checksum = int(parts[0])
        except ValueError:
            pass
    return checksum, elapsed

def main():
    print("=" * 80)
    print("NOQERI MULTI-LANGUAGE BENCHMARK SUITE")
    print(f"Warmups: {WARMUPS} runs | Timed Batches: {BATCHES} runs (reporting median)")
    print("=" * 80)

    for w in WORKLOADS:
        print(f"\n### Workload: {w['name'].upper()} ({w['description']})")
        print(f"Expected Checksum: {w['expected_checksum']}")
        print(f"{'Implementation':<26} | {'Median Time (s)':<15} | {'Checksum':<18} | {'Status'}")
        print("-" * 75)
        
        c_time = None
        for label, cmd in w["commands"]:
            # Warmups
            for _ in range(WARMUPS):
                run_cmd(cmd)
            
            times = []
            checksum = None
            for _ in range(BATCHES):
                cs, t = run_cmd(cmd)
                times.append(t)
                checksum = cs
            
            med_time = statistics.median(times)
            if "C (gcc" in label:
                c_time = med_time
            
            verified = (checksum == w["expected_checksum"])
            status = "PASS" if verified else f"MISMATCH ({checksum})"
            
            print(f"{label:<26} | {med_time:<15.6f} | {str(checksum):<18} | {status}")

if __name__ == "__main__":
    main()
