#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
"$compiler" check Lib/std/render3d.noqeri
"$compiler" build Lib/std/render3d.noqeri "$work/render3d.s" -O3
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pthread \
  "$work/render3d.s" tests/cpp/render3d_differential.cpp -o "$work/render3d-test"
# Any codegen regression that requests an executable ELF stack is unsafe.
readelf -W -l "$work/render3d-test" |
  awk '$1=="GNU_STACK" {found=1; if($7 ~ /E/)bad=1} END {if(!found || bad)exit 1}'
timeout 90s "$work/render3d-test"
"$compiler" check examples/render3d_demo.noqeri
"$compiler" build examples/render3d_demo.noqeri "$work/render3d-demo.s" -O3
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pthread \
  "$work/render3d-demo.s" tests/cpp/render3d_headless_demo.cpp -o "$work/render3d-demo"
timeout 20s "$work/render3d-demo"
if [[ -n "$(printenv GRAPHICS_EVIDENCE_DIR || true)" ]]; then
  evidence="$(printenv GRAPHICS_EVIDENCE_DIR)"
  mkdir -p "$evidence"
  timeout 90s "$work/render3d-test" --bench > "$evidence/render3d-raw.csv"
  node tests/scripts/render3d-performance-summary.mjs "$evidence/render3d-raw.csv" \
    > "$evidence/render3d-summary.json"
  # Compile the original 3D source with this SAME compiler and C++ driver.
  # This proves speedup claims are source-to-source, not cross-machine.
  cp tests/fixtures/render3d_baseline.txt "$work/render3d-baseline.noqeri"
  "$compiler" build "$work/render3d-baseline.noqeri" "$work/render3d-baseline.s" -O3
  g++ -std=c++17 -O2 -Wall -Wextra -Werror -pthread \
    "$work/render3d-baseline.s" tests/cpp/render3d_differential.cpp -o "$work/render3d-baseline"
  timeout 90s "$work/render3d-baseline" --bench > "$evidence/render3d-baseline-raw.csv"
  node tests/scripts/render3d-ab-summary.mjs "$evidence/render3d-baseline-raw.csv" \
    "$evidence/render3d-raw.csv" > "$evidence/render3d-ab-summary.json"
  cp "$work/render3d.s" "$evidence/render3d.s"
  cp "$work/render3d-test" "$evidence/render3d.bin"
  cp "$work/render3d-baseline.s" "$evidence/render3d-baseline.s"
  {
    git rev-parse HEAD
    uname -a
    grep -m1 '^model name' /proc/cpuinfo || true
    printf 'compiler=Noqeri native -O3; cxx=-O2; CPU-only\n'
    sha256sum "$compiler" "$evidence/render3d.s" "$evidence/render3d.bin"
  } > "$evidence/render3d-provenance.txt"
  cat "$evidence/render3d-summary.json"
fi
echo "Noqeri optimized native software 3D raster: PASS"
