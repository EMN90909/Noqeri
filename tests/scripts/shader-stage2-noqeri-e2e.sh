#!/usr/bin/env bash
set -euo pipefail
compiler="${1:?Pass stage-0 compiler path}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
"$compiler" check Compiler/selfhost/shader_wgsl.noqeri
"$compiler" build tests/shader_stage2_native.noqeri "$work/shader_backend.s" -O0
g++ -std=c++17 -Wall -Wextra -Werror -pthread "$work/shader_backend.s" \
  tests/cpp/shader_stage2_native_test.cpp -o "$work/noqeri_shader_backend"
timeout 30s "$work/noqeri_shader_backend" > "$work/noqeri_selfhost_compute.wgsl"
grep -q 'fn noqeri_shade(nq_arg0: u32) -> u32' "$work/noqeri_selfhost_compute.wgsl"
grep -q '@compute @workgroup_size(64)' "$work/noqeri_selfhost_compute.wgsl"
cargo run --quiet --manifest-path tests/wgsl-validator/Cargo.toml -- "$work/noqeri_selfhost_compute.wgsl"
echo "Noqeri-authored standalone selfhost compute shader emitter: PASS"
