#!/usr/bin/env bash
# Local x86-64 native correctness and benchmarking; no GitHub Actions.
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ "$(uname -s)" != Linux || "$(uname -m)" != x86_64 ]]; then
  echo 'Noqeri native qualification requires Linux x86-64.' >&2
  exit 2
fi
for tool in cmake node python3 c++ cc; do command -v "$tool" >/dev/null; done
build_dir="${NOQERI_LOCAL_BUILD_DIR:-build-local-perf}"
cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DNOQERI_WARNINGS_AS_ERRORS=ON
cmake --build "$build_dir" --target noqeri \
  noqeri-owned-collections-test noqeri-owned-value-list-test \
  noqeri-register-allocator-test noqeri-native-bounds-fastpath-test \
  noqeri-owned-alloc-bench noqeri-simd-codegen-test \
  noqeri-optimizer-test noqeri-optimizer-gvn-test \
  noqeri-native-immediate-test noqeri-auto-loop-simd-emitter \
  noqeri-fixed-stack-index-test --parallel 2
ctest --test-dir "$build_dir" --output-on-failure \
  -R '^(noqeri-owned-collections|noqeri-owned-value-list|noqeri-register-allocator-cfg|noqeri-native-bounds-fastpath|noqeri-optimizer-liveness|noqeri-optimizer-gvn|noqeri-native-immediates|noqeri-fixed-stack-index)$'
python3 tests/native_branchfree_division.py --compiler="$build_dir/noqeri"
python3 tests/native_direct_local_moves.py "$build_dir/noqeri"
python3 tests/native_division_traps.py --compiler="$build_dir/noqeri"
python3 tests/native_bounds_local.py --compiler="$build_dir/noqeri"
python3 tests/native_fixed_stack_index_local.py \
  --emitter "$build_dir/noqeri-fixed-stack-index-test"
NOQERI_EXPERIMENTAL_SIMD=1 "$build_dir/noqeri-simd-codegen-test" \
  "$build_dir/simd-broadcast.s"
python3 scripts/local-allocation-profile.py \
  --candidate "$build_dir/noqeri-owned-alloc-bench" \
  --output "$build_dir/allocation-profile.json"
python3 scripts/local-compiler-profile.py \
  --candidate "$build_dir/noqeri" \
  --output "$build_dir/compiler-profile.json"
python3 scripts/local-auto-loop-simd.py \
  --emitter "$build_dir/noqeri-auto-loop-simd-emitter" \
  --output "$build_dir/auto-loop-simd.json"
python3 scripts/local-native-vs-c.py \
  --compiler "$build_dir/noqeri" \
  --output "$build_dir/c-vs-noqeri.json"
if command -v go >/dev/null 2>&1; then
  python3 scripts/local-sorting-matrix-ab.py \
    --candidate "$build_dir/noqeri" \
    --output "$build_dir/sorting-matrix-ab.json"
else
  printf 'Go not found; sorting/matrix Go comparison skipped.\n'
fi
printf '\nLocal qualification and available benchmarks completed.\n'
printf 'Only actual JSON measurements establish runtime speedups.\n'
