#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

ZIG_BIN="${ZIG_BIN:-/home/antigravity/tools/zig}"
NOQERI_BIN="${ROOT_DIR}/build/noqeri"

echo "================================================================================"
echo "          COMPILING BENCHMARK EXECUTABLES FROM SOURCE WITH NATIVE OPT"
echo "================================================================================"

echo "--- Compiler Versions ---"
echo "GCC:    $(gcc --version | head -n 1)"
echo "G++:    $(g++ --version | head -n 1)"
echo "Clang:  $(clang --version | head -n 1)"
echo "Rustc:  $(rustc --version)"
echo "Zig:    $($ZIG_BIN version)"
echo "Go:     $(go version)"
echo "Node:   $(node --version)"
echo "Noqeri: $($NOQERI_BIN --version 2>/dev/null || echo '0.1.4 dev')"
echo ""

cd "${SCRIPT_DIR}"

echo "--- Compiling C (Flags: -O3 -march=native) ---"
gcc -O3 -march=native bench_modulo.c -o c_modulo
gcc -O3 -march=native bench_matrix.c -o c_matrix
gcc -O3 -march=native bench_sort.c -o c_sort
gcc -O3 -march=native bench_image.c -o c_image

echo "--- Compiling C++ (Flags: -O3 -march=native) ---"
g++ -O3 -march=native bench_modulo.cpp -o cpp_modulo
g++ -O3 -march=native bench_matrix.cpp -o cpp_matrix
g++ -O3 -march=native bench_sort.cpp -o cpp_sort
g++ -O3 -march=native bench_image.cpp -o cpp_image

echo "--- Compiling Rust (Flags: -C opt-level=3 -C target-cpu=native) ---"
rustc -C opt-level=3 -C target-cpu=native bench_modulo.rs -o rust_modulo
rustc -C opt-level=3 -C target-cpu=native bench_matrix.rs -o rust_matrix
rustc -C opt-level=3 -C target-cpu=native bench_sort.rs -o rust_sort
rustc -C opt-level=3 -C target-cpu=native bench_image.rs -o rust_image

echo "--- Compiling Zig (Flags: -O ReleaseFast) ---"
$ZIG_BIN build-exe -O ReleaseFast bench_modulo.zig --name zig_modulo
$ZIG_BIN build-exe -O ReleaseFast bench_matrix.zig --name zig_matrix
$ZIG_BIN build-exe -O ReleaseFast bench_sort.zig --name zig_sort
$ZIG_BIN build-exe -O ReleaseFast bench_image.zig --name zig_image

echo "--- Compiling Go (Flags: default release) ---"
go build -o go_modulo bench_modulo.go
go build -o go_matrix bench_matrix.go
go build -o go_sort bench_sort.go
go build -o go_image bench_image.go

echo "--- Compiling Noqeri LLVM AOT (Flags: --backend=llvm -O3) ---"
"$NOQERI_BIN" build bench_modulo.noqeri noqeri_modulo_llvm.s -O3 --backend=llvm
gcc -O3 -I"${ROOT_DIR}/Include/noqeri" noqeri_modulo_llvm.s harness_timed.c -o noqeri_modulo_llvm

"$NOQERI_BIN" build bench_matrix.noqeri noqeri_matrix_llvm.s -O3 --backend=llvm
gcc -O3 -I"${ROOT_DIR}/Include/noqeri" noqeri_matrix_llvm.s harness_timed.c -o noqeri_matrix_llvm

"$NOQERI_BIN" build bench_sort.noqeri noqeri_sort_llvm.s -O3 --backend=llvm
gcc -O3 -I"${ROOT_DIR}/Include/noqeri" noqeri_sort_llvm.s harness_timed.c -o noqeri_sort_llvm

"$NOQERI_BIN" build bench_image.noqeri noqeri_image_llvm.s -O3 --backend=llvm
gcc -O3 -I"${ROOT_DIR}/Include/noqeri" noqeri_image_llvm.s harness_timed.c -o noqeri_image_llvm

echo "--- Compiling Noqeri Direct Native AOT (Flags: --backend=native -O3) ---"
"$NOQERI_BIN" build bench_modulo.noqeri noqeri_modulo_native.s -O3 --backend=native
gcc -O3 -I"${ROOT_DIR}/Include/noqeri" noqeri_modulo_native.s harness_timed.c -o noqeri_modulo_native

"$NOQERI_BIN" build bench_matrix.noqeri noqeri_matrix_native.s -O3 --backend=native
gcc -O3 -I"${ROOT_DIR}/Include/noqeri" noqeri_matrix_native.s harness_timed.c -o noqeri_matrix_native

"$NOQERI_BIN" build bench_sort.noqeri noqeri_sort_native.s -O3 --backend=native
gcc -O3 -I"${ROOT_DIR}/Include/noqeri" noqeri_sort_native.s harness_timed.c -o noqeri_sort_native

"$NOQERI_BIN" build bench_image.noqeri noqeri_image_native.s -O3 --backend=native
gcc -O3 -I"${ROOT_DIR}/Include/noqeri" noqeri_image_native.s harness_timed.c -o noqeri_image_native

echo ""
echo "--- SHA-256 Checksums of Built Executables ---"
sha256sum c_* cpp_* rust_* zig_* go_* noqeri_*_llvm noqeri_*_native
echo ""

echo "================================================================================"
echo "                         RUNNING BENCHMARKS"
echo "================================================================================"
python3 "${SCRIPT_DIR}/rebuild_and_benchmark.py"
