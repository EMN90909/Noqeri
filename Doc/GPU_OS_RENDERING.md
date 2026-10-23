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
shader-specific pure AST emitter. It accepts numeric `u32` inputs and one
return expression made from unsigned constants, explicit `as u32` casts,
parameters, and `+`, `-`, `*`, `/`, `%`. It rejects loops,
mutable state, arbitrary calls, other source declarations, unknown variables,
pointers, FFI, large/negative literals, and malformed stage signatures. This
avoids silently compiling general-purpose Noqeri semantics incorrectly for
a shader target. The shader target is not yet a full Noqeri language profile.

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
python3 -m http.server 8000
```

Open `http://localhost:8000/examples/shaders/webgpu_demo.html` in a
browser with WebGPU enabled and a compatible GPU/driver. The page displays
the fragment gradient and offers a compute button demonstrating real
storage-buffer dispatch and readback. Run from a secure origin such as
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

On desktop systems with SDL2 development libraries, link the generated
assembly with `Runtime/native/os_renderer_sdl.cpp` and SDL2 for native OS
window/presentation integration. The adapter tries hardware-accelerated
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
- Vulkan/DX12/Metal backends, textures/matrices and 3D scene support
- Robust window resize, high-DPI scaling, swapchain recovery,
  presentation pacing and driver/device-loss recovery

These remain explicit rather than described as production-ready.
