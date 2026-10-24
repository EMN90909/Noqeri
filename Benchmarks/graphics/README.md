# Noqeri native graphics benchmark

This is a reproducible **real Noqeri compiler/executable** graphics benchmark,
not a mock benchmark and not a claim about GPU execution.

## Run on Linux x86-64

From repository root, with Python 3, Node 20+, GCC/G++, CMake, and Rust/Cargo
(Naga WGSL validation):

```bash
cmake -S . -B build-graphics -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-graphics --target noqeri -j 2
python3 Benchmarks/graphics/benchmark.py --compiler build-graphics/noqeri
```

Use `--skip-naga` only if Cargo is unavailable. Shader output then remains
**unvalidated**, which is recorded in the result file.

The first stage also checks existing rendering correctness:

```bash
bash tests/scripts/graphics-native-e2e.sh build-graphics/noqeri
```

## What the program actually runs

- **CPU gradient:** `Lib/std/render2d.noqeri` `nqRenderGradient` generates
  ARGB8888 pixels. Native Noqeri builds at `-O3` are linked with a small C++
  benchmark host, which verifies the number of rendered frames.
- **CPU checkerboard:** same architecture, using `nqRenderChecker`.
- **C comparison:** precisely corresponding C raster loops linked with the
  **same** C++ host. The host runs three warm-ups and fifteen timed samples
  **inside each process** using `std::chrono::steady_clock`. Startup,
  compilation and image serialization are excluded from the per-frame times.
- **Pixel correctness:** both executions produce portable PPM (`P6`) images
  from a warmup frame; byte-identical PPM SHA-256 and identical probes are
  required, otherwise the benchmark fails.
- **WGSL shaders:** Noqeri Stage-0 compiles five shader examples repeatedly
  to WGSL. Outputs must have stable SHA-256; Naga checks the resulting WGSL.
  These are **shader compilation times**, not shader execution/FPS.
- **GPU capability:** the script records `/dev/dri` devices; it does not try
  to claim physical GPU performance. Headless runner pixel rendering is CPU.

Scenes: 64² gradient (50 frames per call), 256² gradient (20), 512² gradient
(8), 256² checkerboard (20).

## Results and interpretation

Output `build-graphics/benchmark/graphics-results.json` includes original
Noqeri and C binary hashes, compiler and source hashes, per-sample timings,
median, p95, and PNG-convertible `.ppm` files. The `generated` folder contains
the actual Noqeri source passed to the compiler.

`noqeri_fps_equivalent = 1000 / ms_per_frame` is **CPU-only renderer
throughput**, not screen presentation FPS and not a physical GPU FPS.

A successful benchmark demonstrates correct Noqeri native raster execution
and WGSL generation, not a complete game engine, GPU driver, or browser WebGPU
rendering pipeline.

CI workflow: `.github/workflows/noqeri-graphics-performance.yml` on branch
`benchmark/noqeri-graphics-20261024`. The main branch is untouched.
