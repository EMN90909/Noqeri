#!/usr/bin/env bash
set -euo pipefail
compiler="${1:?pass the built Noqeri compiler}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
"$compiler" check Lib/std/render2d_accel.noqeri
"$compiler" build Lib/std/render2d_accel.noqeri "$work/noqeri-accelerated.s" -O3
g++ -std=c++17 -O3 -Wall -Wextra -Werror -pthread -IInclude \
  "$work/noqeri-accelerated.s" Runtime/native/graphics_fastpath.cpp \
  tests/cpp/graphics_fastpath_native_test.cpp -o "$work/fastclear"
readelf -W -l "$work/fastclear" |
  awk '$1=="GNU_STACK" {found=1; if($7 ~ /E/)bad=1} END {if(!found || bad)exit 1}'
timeout 45s "$work/fastclear"
evidence="${GRAPHICS_EVIDENCE_DIR:-}"
if [[ -n "$evidence" ]]; then
  mkdir -p "$evidence"
  timeout 45s "$work/fastclear" --bench > "$evidence/native-clear-raw.csv" 2> "$evidence/native-clear-checks.txt"
  sha256sum "$work/fastclear" "$work/noqeri-accelerated.s" > "$evidence/native-clear-sha256.txt"
  {
    date -u
    git rev-parse HEAD
    uname -a
    grep -m1 '^model name' /proc/cpuinfo || true
    g++ --version | head -1
    printf 'runner_profile=Noqeri AOT -O3 + C++ SSE2/NEON -O3; hot CPU memory; no SDL or GPU\n'
    printf 'frame_clear_target_ns=10000; dimensions=256x256; sample_count=60\n'
  } > "$evidence/native-clear-provenance.txt"
  cat "$evidence/native-clear-checks.txt"
fi
echo "Noqeri native framebuffer SIMD qualification: PASS"
