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
- **Clear** now unrolls nonvolatile stores four at a time with a
  subtraction-based remainder guard and a scalar tail. All slice writes
  retain bounds checking. This improves measured native CPU throughput,
  but does **not** implement SIMD or the outstanding optimized bulk-fill backend.

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

## First independent CI A/B result (2026-10-24)

GitHub Actions [run 40119159745](https://github.com/EMN90909/Noqeri/actions/runs/40119159745)
successfully compiled both native binaries, passed **4,657** complete
buffer and guard tests, and uploaded its raw 30-sample evidence.

| 256-square scene | Pinned baseline median | Optimized median | Speedup |
|---|---:|---:|---:|
| Gradient | 1,040,599.5 ns | 387,218.5 ns | 2.69× |
| Checker | 576,347 ns | 238,967 ns | 2.41× |
| Clear | 182,531 ns | 182,451 ns | 1.00× |

The independently compiled C reference in the same CI run measured
141,474 ns for gradient, 142,106 ns for checker, and 5,174.5 ns
for clear. Thus optimized Noqeri is **still slower than C**, most
markedly for clear. This is CPU-kernel-only evidence on a CI runner.
The historical 256-square C gradient outlier has not been reproduced
or explained; this reference uses the old source's scalar pixel formula.

The CI per-scene results are not release-performance qualifications for
physical GPUs or other host processors. Historical ratios must not be
substituted for same-run measurements.

## Follow-up clear unrolling result (2026-10-24)

GitHub Actions [run 40121310864](https://github.com/EMN90909/Noqeri/actions/runs/40121310864)
passed the complete **4,657-case** native C++ pixel differential and
paired baseline timing gate with the four-store checked scalar clear:

| Scene 256×256 | Original native Noqeri | Optimized Noqeri | Improvement |
|---|---:|---:|---:|
| Gradient | 1.041 ms | 0.374 ms | **2.79×** |
| Checker | 0.576 ms | 0.224 ms | **2.57×** |
| Clear | 0.183 ms | 0.172 ms | **1.06×** |

The 512×512 clear also improved by approximately **1.06×**. This
is a modest improvement: the optimized Noqeri clear remained roughly
**33× slower** than its C reference on the same runner. A truly
optimized nonvolatile bulk-fill implementation, with volatile MMIO
isolation and full semantics verification, remains outstanding.
