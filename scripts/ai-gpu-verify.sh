#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Execute ONLY on a real Linux OpenCL GPU machine. Never label a CPU pass as GPU.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${NOQERI_AI_GPU_BUILD_DIR:-build-ai-gpu-physical}"
EVIDENCE_DIR="${1:-gpu-evidence}"
mkdir -p "$EVIDENCE_DIR"

{
  printf 'commit: '
  git rev-parse HEAD
  printf 'kernel: '
  uname -r
  printf 'runner: %s\n' "${RUNNER_NAME:-manual}"
  printf 'run: %s\n' "${GITHUB_RUN_ID:-local}"
} > "$EVIDENCE_DIR/environment.txt"

if ! command -v clinfo >/dev/null 2>&1; then
  echo 'FAIL: clinfo is required to record the installed OpenCL hardware.' >&2
  exit 1
fi
clinfo > "$EVIDENCE_DIR/clinfo.txt"
clinfo -l | tee "$EVIDENCE_DIR/opencl-platforms.txt"

cmake -S . -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DNOQERI_AI_OPENCL=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DNOQERI_GRAPHICS_PORTABLE_BUILD=ON \
  -DNOQERI_WEB_SQL_DRIVERS=OFF

if ! grep -q -F -- '-DNQAI_HAVE_OPENCL=1' "$BUILD_DIR/compile_commands.json"; then
  echo 'FAIL: Noqeri GPU backend was not compiled with the OpenCL SDK.' >&2
  exit 1
fi
cmake --build "$BUILD_DIR" \
  --target noqeri-ai-gpu-hardware-test noqeri-ai --parallel 2

# Dedicated test deliberately rejects machines with no CL_DEVICE_TYPE_GPU.
# It dispatches GPU forward/backward GEMM and trains a real two-layer model.
"$BUILD_DIR/noqeri-ai-gpu-hardware-test" \
  "$EVIDENCE_DIR/gpu-results.json" | tee "$EVIDENCE_DIR/gpu-test.log"

# CLI integration on the same physical GPU using the public developer command.
printf 'abcd abcd abcd\n%.0s' {1..6} > "$EVIDENCE_DIR/corpus.txt"
"$BUILD_DIR/noqeri-ai" transformer train "$EVIDENCE_DIR/corpus.txt" \
  "$EVIDENCE_DIR/trained.nqtf" \
  --backend opencl --precision fp32 --optimizer adamw \
  --layers 2 --context 3 --hidden 6 --merges 2 \
  --steps 4 --batch 2 --rate 0.003 --seed 1234 \
  | tee "$EVIDENCE_DIR/training.log"
"$BUILD_DIR/noqeri-ai" transformer evaluate \
  "$EVIDENCE_DIR/trained.nqtf" "$EVIDENCE_DIR/corpus.txt" \
  --backend opencl | tee "$EVIDENCE_DIR/evaluation.log"

echo "PASS: physical OpenCL GPU evidence saved to $EVIDENCE_DIR"
