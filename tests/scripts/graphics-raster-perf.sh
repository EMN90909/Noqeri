#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
out="$(printenv GRAPHICS_EVIDENCE_DIR || true)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

"$compiler" check Lib/std/render2d.noqeri
"$compiler" build Lib/std/render2d.noqeri "$work/optimized.s" -O3
g++ -std=c++17 -O3 -Wall -Wextra -Werror -pthread \
    "$work/optimized.s" tests/cpp/graphics_full_frame_differential.cpp \
    -o "$work/optimized"
timeout 90s "$work/optimized"

if [[ -n "$out" ]]; then
    mkdir -p "$out"
    if ! git cat-file -e '95ecefdd96add7fea5fb5f6a2303f240eb5f1a3f:Lib/std/render2d.noqeri' 2>/dev/null; then
        echo 'Missing pinned pre-optimization baseline source' >&2
        exit 1
    fi
    git show '95ecefdd96add7fea5fb5f6a2303f240eb5f1a3f:Lib/std/render2d.noqeri' > "$work/baseline.noqeri"
    "$compiler" build "$work/baseline.noqeri" "$work/baseline.s" -O3
    g++ -std=c++17 -O3 -Wall -Wextra -Werror -pthread \
        "$work/baseline.s" tests/cpp/graphics_full_frame_differential.cpp \
        -o "$work/baseline"
    timeout 90s "$work/baseline" --bench > "$out/baseline-raw.csv"
    timeout 90s "$work/optimized" --bench > "$out/optimized-raw.csv"
    node tests/scripts/graphics-performance-summary.mjs \
        "$out/baseline-raw.csv" "$out/optimized-raw.csv" \
        > "$out/graphics-summary.json"
    cp "$work/baseline.s" "$out/baseline.s"
    cp "$work/optimized.s" "$out/optimized.s"
    cp "$work/baseline" "$out/baseline.bin"
    cp "$work/optimized" "$out/optimized.bin"
    {
        printf 'source_commit='
        git rev-parse HEAD
        printf 'baseline_source_commit=95ecefdd96add7fea5fb5f6a2303f240eb5f1a3f\n'
        uname -a
        grep -m1 '^model name' /proc/cpuinfo || true
        printf 'compiler_sha256='
        sha256sum "$compiler"
        g++ --version | head -1
        printf 'compiler_flags=-O3; cxx_flags=-O3\n'
        sha256sum "$out"/*.s "$out"/*.bin
    } > "$out/provenance.txt"
    cat "$out/graphics-summary.json"
fi
echo "Noqeri CPU raster full-frame qualification: PASS"
