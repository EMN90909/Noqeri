#!/usr/bin/env bash
set -euo pipefail
compiler="${1:?pass the built Noqeri compiler}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
# Prefer the actual CMake-produced static library in CI; standalone users
# may omit the variable and build the exact same C++ implementation directly.
backend=(Runtime/native/graphics_fastpath.cpp)
if [[ -n "${NOQERI_GRAPHICS_FASTPATH_LIB:-}" ]]; then
  [[ -f "$NOQERI_GRAPHICS_FASTPATH_LIB" ]] || {
    echo "Missing CMake graphics library: $NOQERI_GRAPHICS_FASTPATH_LIB" >&2
    exit 1
  }
  backend=("$NOQERI_GRAPHICS_FASTPATH_LIB")
fi
"$compiler" check Lib/std/render2d_accel.noqeri
"$compiler" build Lib/std/render2d_accel.noqeri "$work/noqeri-accelerated.s" -O3
g++ -std=c++17 -O3 -Wall -Wextra -Werror -pthread -IInclude \
  "$work/noqeri-accelerated.s" tests/cpp/graphics_fastpath_native_test.cpp \
  "${backend[@]}" -o "$work/fastclear"
readelf -W -l "$work/fastclear" |
  awk '$1=="GNU_STACK" {found=1; if($7 ~ /E/)bad=1} END {if(!found || bad)exit 1}'
timeout 45s "$work/fastclear"
"$compiler" check Lib/std/render3d_accel.noqeri
"$compiler" build Lib/std/render3d_accel.noqeri "$work/noqeri-depth-accelerated.s" -O3
g++ -std=c++17 -O3 -Wall -Wextra -Werror -pthread -IInclude \
  "$work/noqeri-depth-accelerated.s" tests/cpp/graphics_depth_fastpath_native_test.cpp \
  "${backend[@]}" -o "$work/depth-clear"
readelf -W -l "$work/depth-clear" |
  awk '$1=="GNU_STACK" {found=1; if($7 ~ /E/)bad=1} END {if(!found || bad)exit 1}'
timeout 45s "$work/depth-clear"
echo "Noqeri native framebuffer SIMD qualification: PASS"
