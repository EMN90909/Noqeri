# GPU shaders and OS rendering in Noqeri

This is the first **executable** Noqeri-authored graphics integration, not a
claim that Noqeri ships a custom GPU driver or complete game engine.

## GPU shader source in Noqeri

WebGPU shaders are authored as ordinary `.noqeri` source with exactly one
exported, pure `shade` function, and compiled using a new compiler target:

```sh
noqeri shader examples/shaders/color_gradient.noqeri build/gradient.wgsl --stage=fragment
noqeri shader examples/shaders/invert_compute.noqeri build/invert.wgsl --stage=compute
```

The **fragment** profile requires `export function shade(x: u32, y: u32): u32`
and returns a packed 24-bit RGB color, converted to a WebGPU float RGBA output
with alpha = 1.0. Its coordinates come from `@builtin(position)`.

The **compute** profile requires `export function shade(value: u32): u32`.
The generated WGSL declares input at group 0 binding 0 (read-only storage),
output at group 0 binding 1 (read/write storage), and dispatches with 64
invocations per workgroup. The generated entry checks **both** buffer
boundaries before reading/writing. An application must create WebGPU buffers,
bind them, and issue `ceil(pixel_count/64)` workgroups. A GPU device and
runtime are **not** embedded in this compiler. A small browser-only
WebGPU host in `examples/shaders/webgpu_host.mjs` can compile the output
into actual browser compute and render pipelines on supported devices.
This is necessary OS/browser interop, not replacement shader logic.

The compiler runs the existing Noqeri parser and type checker, then a
bounded, shader-specific pure AST emitter. **Shader v2** accepts `u32` and
`float` signatures, immutable typed locals, conditional `if/else`, pure
arithmetic, comparisons, and boolean operators. The Noqeri `float` source
type maps to WebGPU `f32` and therefore loses precision compared with
the CPU floating-point type. GPU shader names are hygienically mapped to
WGSL symbols, preventing source keyword clashes.

The compiler still rejects loops, mutable state, arbitrary host calls,
pointers, FFI, unsupported types and stages. Compute shader workgroup size
is configurable with `--workgroup-size=32|64|128|256`. The WebGPU browser
adapter reads that generated workgroup size and checks the adapter's hardware
limits. See [Shader v2 details and examples](GPU_SHADER_V2.md).

CI compiles both Noqeri source examples to WGSL and validates the generated
programs using the **Naga WGSL frontend and shader validator**, not just
text matching. Noqeri's compiler does not directly emit SPIR-V, HLSL, MSL or
GPU machine instructions in this phase. Browser runtime code uses
`navigator.gpu` and reports device/module/pipeline failures explicitly.
Actual driver/device qualification remains a separate acceptance requirement.

### Run the shaders in a WebGPU browser

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target noqeri -j 2
build/noqeri shader examples/shaders/color_gradient.noqeri build/gradient.wgsl --stage=fragment
build/noqeri shader examples/shaders/invert_compute.noqeri build/invert.wgsl --stage=compute
build/noqeri shader examples/shaders/posterize_fragment.noqeri build/posterize.wgsl --stage=fragment
build/noqeri shader examples/shaders/grayscale_fragment.noqeri build/grayscale.wgsl --stage=fragment
build/noqeri shader examples/shaders/float_compute.noqeri build/float-compute.wgsl --stage=compute --workgroup-size=128
python3 -m http.server 8000
```

Open `http://localhost:8000/examples/shaders/webgpu_demo.html` in a
browser with WebGPU enabled and a compatible GPU/driver. The page offers gradient, posterized and grayscale fragment previews,
plus separate integer and floating-point compute dispatch buttons with
storage-buffer readback. It releases GPU devices when switching shaders. Run from a secure origin such as
`localhost`; the browser host is optional and is not used by the
Noqeri native SDL2 CPU renderer.

## OS window and framebuffer rendering

Pixel filling and geometry are authored in
`Lib/std/render2d.noqeri`. Functions include:

- `nqArgb8888(r,g,b)`: canonical ARGB8888 packing
- `nqRenderClear(frame, color)`: fill a bounded u32 slice
- `nqRenderGradient(frame,width,height)`: bounded x/y RGB gradient
- `nqRenderChecker(frame,width,height,cell,first,second)`: checker pattern

The OS example `examples/graphics_demo.noqeri` allocates a fixed Noqeri
framebuffer, passes it to Noqeri rasterizer functions, and presents the
pixels via `nq_os_renderer_present`, passing the actual buffer length as
an additional guard against out-of-bounds native texture uploads. This call
is a **narrow native OS bridge**,
not a C++ rewrite of the rasterizer.

Compile the app to freestanding x86-64 Noqeri assembly:

```sh
noqeri build examples/graphics_demo.noqeri build/graphics.s -O0
```

On desktop systems with SDL2 development libraries, CMake exposes an
optional reusable `noqeri_os_renderer_sdl` static library with a small
`Include/noqeri/os_renderer_sdl.h` ABI. Link your generated assembly and
host entrypoint against that library for native OS window/presentation.
For a standalone demo with a test entrypoint, the integration script
compiles the SDL2 adapter using `NOQERI_OS_RENDERER_TEST_APP`.
The bridge is deliberately not a mandatory dependency of the Noqeri
compiler, native runtime or kernel framebuffer library. The adapter tries hardware-accelerated
SDL rendering first, falls back to software, checks texture dimensions and
format, and pumps window events. SDL2 abstracts Linux, Windows and macOS
window systems. **Native Noqeri AOT x86-64 ABI support is currently verified
only on Linux**, so SDL portability does not yet prove Noqeri-produced
Windows PE or macOS Mach-O executables.

The CI test runs a **real compiled Noqeri program** with a headless pixel
presenter and validates pixels at the four corners and interior, then launches
the same native program using SDL2's `dummy` video driver. This proves
end-to-end rendering and presentation API integration on the runner, not a
physical display, real GPU shader dispatch, Vulkan, Metal, Direct3D or a
self-contained OS compositor.

To reproduce in a suitable Linux checkout with SDL2 installed:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target noqeri -j 2
NOQERI_REQUIRE_SDL2=1 bash tests/scripts/graphics-native-e2e.sh build/noqeri
```

Do not conflate CPU drawing via an accelerated OS SDL compositor with
GPU-executed Noqeri WGSL. They are distinct backends and require a future
shader dispatch pipeline or browser WebGPU app to connect them.

## Kernel and bare-metal framebuffers in Noqeri

`Lib/std/os_framebuffer.noqeri` also implements direct ARGB8888
**volatile MMIO/framebuffer pointer** access, stride-aware clear and
checker drawing, fully in Noqeri without SDL2 or any OS runtime.
The kernel or bootloader must provide a valid mapped writable
`*volatile u32` buffer, dimensions and row pitch (in pixels).

`examples/os_framebuffer_demo.noqeri` calls the Noqeri functions with an
external framebuffer memory provider. The native integration test compiles
both the Noqeri code and a minimal memory provider and proves that
stride/pitch padding remains untouched. This tests mapping semantics
against simulated memory, **not** a physical GPU or actual OS boot
surface. It is explicitly **not** a GPU device driver, PCI discovery,
display controller, DRM/KMS stack, or an OS compositor. For that work,
a real OS must provide display device discovery, memory mapping, page
flipping, permissions, input and lifecycle management.

## Not yet complete

- Native Stage-2 compiler/self-hosted shader code generation
- WebGPU device enumeration, shader module creation, pipeline/bindings
  and actual GPU compute/fragment dispatch in a Noqeri-owned runtime
- Direct Windows/macOS native executable linking and platform CI graphics tests
- General-purpose Vulkan/DX12/Metal backends, matrix/vertex-attribute pipelines,
  GPU driver qualification, and production 3D scene support.
  Experimental checked native CPU triangles, depth testing, projection,
  WebGPU vertex shaders and depth-attachment hosting are documented in
  [RENDER3D_QUALIFICATION.md](RENDER3D_QUALIFICATION.md).
- Robust window resize, high-DPI scaling, swapchain recovery,
  presentation pacing and driver/device-loss recovery

These remain explicit rather than described as production-ready.

## Experimental GPU firmware development

Noqeri also provides a restricted, x86-64 simulator-only profile for
firmware experiments with bounded volatile register operations, polling,
toy mailbox execution and checksum-verified offline development artifacts.
This **does not** program a physical GPU or generate vendor GPU microcode.
See [GPU firmware development](GPU_FIRMWARE_DEVELOPMENT.md) and the
[3D rendering qualification](RENDER3D_QUALIFICATION.md).

## Crisp, lightweight desktop presentation

The SDL2 bridge now preserves the pixel aspect ratio when the desktop
window is resized. Instead of unconditionally stretching the framebuffer,
it computes a centered letterboxed viewport using safe 64-bit integer
arithmetic, and reuses the existing SDL texture and renderer.

The optional `nq_os_renderer_set_scale_mode(mode)` supports:

| Mode | Filtering | Placement | Best use |
|---|---|---|---|
| 0 (default) | Nearest | Integer upscale where it fits, otherwise aspect-fit | Sharp low-resolution / pixel-art graphics |
| 1 | Linear | Aspect-fit | Smooth scaled photography, gradients |
| 2 | Nearest | Aspect-fit at any scale | Crisp pixels at arbitrary window sizes |

The renderer clears the letterbox background to opaque black and
calculates the viewport from the actual SDL render-output dimensions
each frame, without recreating the texture on window resize. The pure
`nq_os_renderer_fit_viewport` ABI is tested against wide-to-square,
noninteger, tiny-window and invalid-dimension cases.
Nearest-neighbor scaling does not add detail to a low-resolution source:
higher native framebuffer resolution is still required for detailed
3D scenes. Integer scaling avoids uneven widths on pixel art.

### WebGPU frame pacing and optional edge quality

The persistent 3D renderer retains `await renderer.render(vertices)`,
which waits for queue completion to preserve its original contract.
For responsive frame loops, `await renderer.renderFast(vertices)`
allows two queued frames without waiting and fences on the third to
bound GPU submissions. `await renderer.flush()` synchronizes explicitly,
and resizing is blocked until outstanding quick-submitted frames are
flushed. This reduces unnecessary per-frame synchronization, not
render workload or physical GPU frame time.

To soften jagged edges in 3D, enable optional `{sampleCount:4}` as
the sixth argument to `createNoqeriMeshRenderer3D`. It adds
multisampled color/depth attachments and resolves color to the swapchain.
The default `sampleCount:1` remains lighter. For predictable resource
usage, 4x MSAA is limited to 1920x1080 backing pixels, while native
resolution and the 1x profile can use the established dimension cap.
For example:

```js
const renderer = await createNoqeriMeshRenderer3D(
  canvas, vertexWGSL, fragmentWGSL, navigator.gpu, 4095,
  {sampleCount: 4} // optional: prefer 1x for minimum GPU memory
)
await renderer.renderFast(vertices)
await renderer.flush() // required before replacing depth on resize
renderer.resize(canvas.width, canvas.height)
renderer.dispose()
```

Both modes preserve resource ownership and GPU validation, but real
end-to-end FPS, device memory and image-quality qualification still
requires a physical GPU, driver/version inventory, and captured test
frames. These optimizations must not be described as measured hardware
speedups without that evidence.


## Lightweight framebuffer and GPU texture profiles (2026-10-25)

The native kernel framebuffer checker now fills solid horizontal tile runs
and advances tile-row parity with a counter. Its hot pixel path no longer
divides by the tile size or multiplies by the stride. The volatile pointer
and every required pixel write are preserved: no nonvolatile memset,
cache assumptions, or SIMD writes are used on memory-mapped framebuffer
surfaces. The corresponding native C++ oracle validates multiple pitches,
row padding, odd cells, invalid dimensions, and guard sentinels.

The SDL2 bridge skips clearing the window when the centered destination
covers the entire render output. It still clears black letterbox bars when
the aspect ratios differ, preserving sharp nearest-neighbor scaling.

Textured WebGPU fragments accept an optional fifth parameter:

```js
const release = await renderNoqeriFragment(canvas, wgsl, gpu, rgbaTexture,
  {textureFilter: 'nearest'}) // default, preserves hard pixel boundaries
// or {textureFilter: 'linear'} for photographs and smooth illustrations
```

Texture sampling remains explicitly selectable: nearest is now the default
to prevent unintended texture blur. This setting changes sampling quality,
not resolution or actual source detail. The 3D WebGPU paths also discard
cleared depth attachments after each pass, and 4x MSAA resolves the
multisampled color and discards its temporary sample surface. Because the
next frame always clears depth, storing it is unnecessary. This can reduce
transient memory bandwidth on tile-based GPUs, but **GPU speedup has not
been measured on physical hardware**.

Run the graphics workflow for compiler/native qualification; it archives
raw 30-sample native volatile checker frame timings as
`os-framebuffer-native.csv`. These timings exclude the SDL presenter and
WebGPU GPU completion. Compare results on the same hardware before
claiming a performance improvement.
