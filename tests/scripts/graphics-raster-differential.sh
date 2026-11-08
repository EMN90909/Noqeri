#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

"$compiler" check Lib/std/render2d.noqeri
"$compiler" build Lib/std/render2d.noqeri "$work/optimized.s" -O3
g++ -std=c++17 -O3 -Wall -Wextra -Werror -pthread \
    "$work/optimized.s" tests/cpp/graphics_full_frame_differential.cpp \
    -o "$work/optimized"
# Any codegen regression that requests an executable ELF stack is unsafe.
readelf -W -l "$work/optimized" |
  awk '$1=="GNU_STACK" {found=1; if($7 ~ /E/)bad=1} END {if(!found || bad)exit 1}'
timeout 90s "$work/optimized"

echo "Noqeri native full-frame raster differential: PASS"
