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
  -DBUILD_TESTING=ON -DNOQERI_WARNINGS_AS_ERRORS=ON \
  -DNOQERI_GRAPHICS_PORTABLE_BUILD=ON
cmake --build "$build_dir" --target noqeri \
  noqeri-owned-collections-test noqeri-owned-value-list-test \
  noqeri-register-allocator-test --parallel 2
ctest --test-dir "$build_dir" --output-on-failure \
  -R '^(noqeri-owned-collections|noqeri-owned-value-list|noqeri-register-allocator-cfg)$'
python3 tests/native_branchfree_division.py --compiler="$build_dir/noqeri"
python3 tests/native_direct_local_moves.py "$build_dir/noqeri"
python3 tests/native_division_traps.py --compiler="$build_dir/noqeri"
printf '\nLocal native correctness tests passed.\n'
printf 'No runtime speedup versus C is asserted by this script.\n'
