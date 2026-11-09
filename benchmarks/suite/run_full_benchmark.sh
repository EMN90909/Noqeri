#!/usr/bin/env bash
set -e

echo "=== BENCHMARK SUITE: MODULO, MATRIX MULTIPLICATION, SORTING, IMAGE RENDERING ==="
echo -e "Language\tBenchmark\tResult/Checksum\tTime (seconds)"

# Modulo
./benchmarks/suite/c_modulo
./benchmarks/suite/cpp_modulo
./benchmarks/suite/rust_modulo
./benchmarks/suite/zig_modulo
./benchmarks/suite/go_modulo
node benchmarks/suite/bench_modulo.js
echo -n -e "Noqeri(AOT)\tmodulo\t"
./benchmarks/suite/noqeri_modulo

# Matrix
./benchmarks/suite/c_matrix
./benchmarks/suite/cpp_matrix
./benchmarks/suite/rust_matrix
./benchmarks/suite/zig_matrix
./benchmarks/suite/go_matrix
node benchmarks/suite/bench_matrix.js
echo -n -e "Noqeri(AOT)\tmatrix\t"
./benchmarks/suite/noqeri_matrix

# Sorting
./benchmarks/suite/c_sort
./benchmarks/suite/cpp_sort
./benchmarks/suite/rust_sort
./benchmarks/suite/zig_sort
./benchmarks/suite/go_sort
node benchmarks/suite/bench_sort.js
echo -n -e "Noqeri(AOT)\tsort\t"
./benchmarks/suite/noqeri_sort

# Image
./benchmarks/suite/c_image
./benchmarks/suite/cpp_image
./benchmarks/suite/rust_image
./bench_image
./benchmarks/suite/go_image
node benchmarks/suite/bench_image.js
echo -n -e "Noqeri(AOT)\timage\t"
/tmp/bench_noqeri_image
