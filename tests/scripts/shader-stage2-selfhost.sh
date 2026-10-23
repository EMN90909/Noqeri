#!/usr/bin/env bash
set -euo pipefail
vm="${1:?pass noqeri-nqri VM}"
stage0="${2:?pass Stage-0 compiler}"
stage2="${3:?pass Stage-2 compiler artifact}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

"$vm" has-export "$stage2" stage2CompileToNqri
"$vm" compile-manifest "$stage2" . Compiler/shaders/stage2-shader-sources.txt "$work/stage2-shader.nqri" 3
"$vm" has-export "$work/stage2-shader.nqri" stage2ShaderCompile

"$stage0" shader examples/shaders/invert_compute.noqeri "$work/stage0-invert.wgsl" --stage=compute --workgroup-size=128
"$vm" shader "$work/stage2-shader.nqri" examples/shaders/invert_compute.noqeri "$work/stage2-invert.wgsl" compute 128
if ! cmp "$work/stage0-invert.wgsl" "$work/stage2-invert.wgsl"; then
  echo "Stage-2 Noqeri shader output differs from Stage-0 for compute" >&2
  diff -u "$work/stage0-invert.wgsl" "$work/stage2-invert.wgsl" || true
  exit 1
fi

"$stage0" shader examples/shaders/color_gradient.noqeri "$work/stage0-gradient.wgsl" --stage=fragment
"$vm" shader "$work/stage2-shader.nqri" examples/shaders/color_gradient.noqeri "$work/stage2-gradient.wgsl" fragment
if ! cmp "$work/stage0-gradient.wgsl" "$work/stage2-gradient.wgsl"; then
  echo "Stage-2 Noqeri shader output differs from Stage-0 for fragment" >&2
  diff -u "$work/stage0-gradient.wgsl" "$work/stage2-gradient.wgsl" || true
  exit 1
fi

# Unsupported and impure programs must be refused by the self-hosted target.
if "$vm" shader "$work/stage2-shader.nqri" examples/shaders/texture_fragment.noqeri "$work/rejected.wgsl" fragment; then
  echo "Stage-2 target accepted unsupported texture compile" >&2
  exit 1
fi
test ! -e "$work/rejected.wgsl"
printf 'stage2_shader_selfhost_compiled=true\n'
printf 'stage2_shader_codegen_owned_by_noqeri=true\n'
printf 'stage2_shader_scalar_compute_byte_equal=true\n'
printf 'stage2_shader_scalar_fragment_byte_equal=true\n'
printf 'stage2_shader_vector_texture_parity=false\n'
printf 'stage2_execution_model=nqri-vm\n'
echo "Noqeri-written Stage-2 scalar shader WGSL parity: PASS"
