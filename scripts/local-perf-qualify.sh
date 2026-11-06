#!/usr/bin/env bash
# Standalone, local-only native-performance correctness qualification.
# This script deliberately does not use GitHub Actions or any remote runner.
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ "$(uname -s)" != Linux || "$(uname -m)" != x86_64 ]]; then
  echo 'Native x86-64 AOT qualification currently requires Linux x86-64.' >&2
  exit 2
fi
command -v cmake >/dev/null
command -v node >/dev/null
command -v python3 >/dev/null
command -v c++ >/dev/null
build_dir="${NOQERI_LOCAL_BUILD_DIR:-build-local-perf}"
cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DNOQERI_WARNINGS_AS_ERRORS=ON
cmake --build "$build_dir" --target noqeri \
  noqeri-owned-collections-test noqeri-owned-value-list-test \
  noqeri-register-allocator-test noqeri-native-bounds-fastpath-test \
  noqeri-owned-alloc-bench noqeri-simd-codegen-test \
  noqeri-optimizer-test --parallel 2
ctest --test-dir "$build_dir" --output-on-failure \
  -R '^(noqeri-owned-collections|noqeri-owned-value-list|noqeri-register-allocator-cfg|noqeri-native-bounds-fastpath|noqeri-optimizer-liveness)
python3 tests/native_branchfree_division.py --compiler="$build_dir/noqeri"
python3 tests/native_direct_local_moves.py "$build_dir/noqeri"
python3 tests/native_division_traps.py --compiler="$build_dir/noqeri"
python3 tests/native_bounds_local.py --compiler="$build_dir/noqeri"
NOQERI_EXPERIMENTAL_SIMD=1 "$build_dir/noqeri-simd-codegen-test" \
  "$build_dir/simd-broadcast.s"
python3 scripts/local-allocation-profile.py \
  --candidate "$build_dir/noqeri-owned-alloc-bench" \
  --output "$build_dir/allocation-profile.json"
python3 scripts/local-native-vs-c.py \
  --compiler "$build_dir/noqeri" \
  --output "$build_dir/c-vs-noqeri.json"
printf '\nLocal native correctness tests passed.\n'
printf 'No runtime speedup versus C is asserted by this script.\n'

python3 tests/native_branchfree_division.py --compiler="$build_dir/noqeri"
python3 tests/native_direct_local_moves.py "$build_dir/noqeri"
python3 tests/native_division_traps.py --compiler="$build_dir/noqeri"
python3 tests/native_bounds_local.py --compiler="$build_dir/noqeri"
python3 scripts/local-allocation-profile.py \
  --candidate "$build_dir/noqeri-owned-alloc-bench" \
  --output "$build_dir/allocation-profile.json"
printf '\nLocal native correctness tests passed.\n'
printf 'No runtime speedup versus C is asserted by this script.\n'
