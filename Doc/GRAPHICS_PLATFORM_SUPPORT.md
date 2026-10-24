# Graphics compiler, framebuffer and GPU portability

The portable graphics profile builds the **real Noqeri Stage-0 C++ shader
compiler** and a separately linked C++ framebuffer clear library. Shader
source is authored in Noqeri and emitted as WebGPU **WGSL**. No vendor GPU
microcode is emitted. This is deliberately not a claim that the whole native
Noqeri compiler, assembler, linker or JIT works on every processor.

## Build without platform-specific database/mailbox/PGAS dependencies

```sh
cmake -S . -B build-graphics -DCMAKE_BUILD_TYPE=Release \
  -DNOQERI_GRAPHICS_PORTABLE_BUILD=ON -DBUILD_TESTING=ON
cmake --build build-graphics --config Release \
  --target noqeri noqeri-graphics-portability-test --parallel 2
ctest --test-dir build-graphics -C Release --output-on-failure \
  -R '^noqeri-graphics-'
```

This qualifies the same compiler's vertex, fragment and compute WGSL
emission, deterministic repeated output, invalid resource access rejection,
and native framebuffer color/pitch correctness. On Windows using a
multi-configuration build, use `build-graphics/Release/noqeri.exe`; on
single-configuration builds use `build-graphics/noqeri`.

To exercise the safe, architecture-independent CPU kernel regardless of
host SIMD support:

```sh
cmake -S . -B build-graphics-scalar -DCMAKE_BUILD_TYPE=Release \
  -DNOQERI_GRAPHICS_PORTABLE_BUILD=ON -DBUILD_TESTING=ON \
  -DNOQERI_GRAPHICS_FORCE_SCALAR=ON
cmake --build build-graphics-scalar --config Release \
  --target noqeri-graphics-portability-test
ctest --test-dir build-graphics-scalar -C Release \
  -R '^noqeri-graphics-portability$' --output-on-failure
```

The native C ABI `nq_graphics_clear_backend()` identifies the compiled
fastpath as `sse2`, `neon` or `scalar`; the `nq_graphics_cpu_arch()`
and `nq_graphics_host_os()` probes describe only compiled target strings.
They do not enumerate physical GPUs. The binary never executes SSE2 on
x86-32 without compiler-enabled SSE2, and NEON is guarded by the toolchain.
Fallback CPU architectures run exact same-pixel scalar clearing.

## Build and verification matrix

| Host OS | Shader WGSL build target | CPU clear kernel | Qualification |
|---|---|---|---|
| Linux x86-64 | C++ compiler on host | SSE2 + scalar test | Native CI |
| Linux ARM64 | C++ compiler on host | NEON + scalar test | Native CI |
| Windows x86-64 | MSVC compiler on host | SSE2 + scalar test | Native CI |
| macOS ARM64 | Apple Clang compiler on host | NEON + scalar test | Native CI |
| macOS x86-64 | Apple Clang compiler on host | SSE2 + scalar test | Native CI |
| FreeBSD x86-64 | Clang compiler in VM | SSE2 | VM CI |
| OpenBSD x86-64 | Clang compiler in VM | SSE2 | VM CI |
| Android ARM64, ARMv7, x86-64 | Cross-compiled C++ Noqeri shader compiler, not executed | NEON where enabled / SSE2 / scalar | NDK cross-compile/link, not device execution |
| RISC-V, PowerPC, s390x and other targets | Requires separately qualified C++ toolchain/compiler | Scalar fallback available | No hardware CI proof |

The matrix describes **test intent and coverage**, not a blanket success
claim. Always inspect `.github/workflows/graphics-portability.yml` run
conclusions and job logs before treating a platform as verified.
FreeBSD/OpenBSD jobs run virtual machines; Android uses an NDK cross-compiler
and cannot run an Android binary on the Linux runner.

The portable CMake mode also exposes the optional `noqeri_os_renderer_sdl`
window/presentation target when a usable SDL2 CMake package is installed.
A successful headless frame test does not prove a physical display or GPU.

Graphics-only CI is separate from the Linux-specific native x86-64
Noqeri AOT integration suites. SDL2 native presentation is a separate
optional adapter. A successful WGSL compiler or CPU framebuffer library
build must not be described as full Noqeri native application/AOT support.

## GPU and video-card architectures

Noqeri WGSL targets the **WebGPU shader programming model**, not a GPU ISA.
It can be consumed by a compatible WebGPU implementation backed by Vulkan,
Metal or Direct3D 12. The operating system, GPU vendor, driver, device
feature set and WebGPU implementation determine whether a pipeline can
run. Supported device vendors can include AMD, NVIDIA, Intel, Apple and
mobile GPUs if the actual driver/runtime meets WebGPU requirements, but
**none of those combinations is individually qualified by this CI**.

A browser-specific WebGPU bridge is isolated in
`examples/browser-webgpu/`, while Noqeri shader sources remain
JavaScript-free in `examples/shaders/`. Native C++ GPU-device creation,
Vulkan/Metal/D3D12 shader translation and driver-recovery paths are
**still outstanding**. OpenBSD support for physical accelerated GPU
pipelines is particularly dependent on external drivers and runtimes.

To qualify a GPU, record physical hardware ID, vendor, GPU ISA family,
OS/kernel, driver, WebGPU runtime/browser, generated WGSL validation,
GPU-produced pixel readback, device-loss behavior, reproducible frame
timing, memory use and power consumption. Do not substitute CPU clear
timings, CMake builds or simulated adapters for physical GPU evidence.

## Performance principles

- Prefer the compiled SSE2/NEON fastpath for ordinary **nonvolatile**
  ARGB8888 color or depth buffers when available; never apply bulk SIMD
  stores to volatile/MMIO device memory.
- Keep the pixel bounds proof and stride/pitch guards before writes.
- Use persistent GPU buffers, indexed vertex data and bounded frame
  queuing with optional 4x MSAA; choose 1x MSAA for lower memory/bandwidth.
- Keep compiler WGSL emission independent of GPU vendor and architecture;
  let a qualified runtime optimize that WGSL for actual GPU hardware.
- The previously observed **5.089 microseconds** 256x256 CPU clear was
  measured on one Linux x86-64 runner. It is **not** an upper bound for
  other resolutions, processors or operating systems.
