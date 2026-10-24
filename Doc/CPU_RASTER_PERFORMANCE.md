# Native CPU raster optimization and qualification

This page covers the native CPU ARGB8888 renderer in
`Lib/std/render2d.noqeri`. It does **not** measure GPU execution,
WebGPU shader throughput, SDL texture uploads or presentation latency.

## Source changes

- **Gradient** uses two nested row/pixel loops. All width/height quotient
  and remainder calculations occur once per render, with exact integer
  carry-based interpolation for both channels. The color packer is
  specialized for fixed blue=64 and bounded integer R/G values.
  No coordinate division or helper call occurs within the pixel loop.
- **Checker** paints horizontal runs of equal color and flips row parity
  at cell boundaries; no per-pixel coordinate or tile division is required.
  Variable cell sizes, including sizes greater than the image, are supported.
- **Clear** retains the original checked scalar loop while the compiler's
  bulk-fill lowering and volatile semantics are separately qualified.
  No unsupported SIMD claims are made.

All dimension and buffer-length guards remain in place. The gradient still
caps both dimensions at 4096; checker retains its original flexible shape
contract. Both guard `height > len(pixels) / width` **after** rejecting
zero width, so `width*height` is safe without overflow for accepted inputs.

## Reproducible whole-frame test and paired performance evidence

With a built Linux x86-64 Noqeri compiler:

```sh
cmake -S . -B /tmp/noqeri-graphics-build -DCMAKE_BUILD_TYPE=Release -DNOQERI_WARNINGS_AS_ERRORS=ON
cmake --build /tmp/noqeri-graphics-build --target noqeri -j 2
GRAPHICS_EVIDENCE_DIR=/tmp/noqeri-graphics-evidence \
  bash tests/scripts/graphics-raster-perf.sh /tmp/noqeri-graphics-build/noqeri
```

The harness builds both the pre-optimization source pinned at
`95ecefdd96add7fea5fb5f6a2303f240eb5f1a3f` and the current kernel
with `-O3`, links each to the **same** C++ independent reference,
checks every output pixel and all buffer sentinels, and then measures
three raster scenes at 64, 256 and 512 square resolutions.

It exercises zero and truncated slices, awkward dimensions and cell
sizes, and uniform-color output. Each scene gets five warmups and
thirty raw per-engine frame timing samples. The benchmark draws into
observable buffers; reads outside each timed render prevent dead-work
elimination. Timing excludes process startup, checksums, SDL and GPU.

CI artifacts include raw CSV, a paired median summary, source commit,
compiler/toolchain and CPU metadata, compiled baseline/new assembly,
and native binaries with SHA-256 hashes. This permits inspecting dynamic
division, call and spill patterns after each change.

## Reading historical CSV

The uploaded aggregate data reported the following 256-square slowdowns
relative to a C reference: gradient 75.49x, checker 23.37x, clear 38.55x.
Those figures did not include raw timing vectors, full reference source,
compiler artifacts or hardware info. In particular, the C gradient row
at 256 square was anomalously faster than the 64 and 512 square rows.

**Never subtract or divide new CI timings by the historical CSV** to claim
a speedup without matched hardware and compiler profiles. Use the
same-run pinned source vs new source medians to evaluate this change.
The benchmark summary is evidence only for native CPU rasterization,
not FPS, GPU hardware or presentation speed.

## Remaining work

Bulk-fill lowering for `nqRenderClear`, SIMD feature-gating and tails,
safe dynamic-slice bounds elimination, improved named-local promotion,
and independent GPU-driver qualification are separate milestones. Keep
volatile framebuffer I/O away from nonvolatile bulk-memory optimizations.
