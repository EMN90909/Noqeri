#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
"$compiler" check Lib/std/render3d.noqeri
"$compiler" build Lib/std/render3d.noqeri "$work/render3d.s" -O3
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pthread \
  "$work/render3d.s" tests/cpp/render3d_differential.cpp -o "$work/render3d-test"
timeout 90s "$work/render3d-test"
echo "Noqeri optimized native software 3D raster: PASS"
