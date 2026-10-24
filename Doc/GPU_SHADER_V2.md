# Noqeri shader profile v2 (experimental)

**Newer profile:** [GPU shader v3](GPU_SHADER_V3.md) adds checked f32 vectors,
2D sampled textures, RGBA fragment output and vec4 compute storage. The
Stage-2 self-hosted shader emitter and physical driver qualification are
still outstanding; refer to v3 documentation for precise acceptance gates.

GPU shaders remain authored in **Noqeri** and compiled by Stage-0 to WebGPU
WGSL. The renderer/device APIs are separate host integrations, not GPU
drivers written in Noqeri.

## Shader stages and source signatures

| Stage | Noqeri source signature | WGSL output |
|---|---|---|
| Compute integer | `export function shade(value: u32): u32` | `array<u32>` input/output |
| Compute float | `export function shade(value: float): float` | `array<f32>` input/output |
| Fragment packed RGB | `export function shade(x: u32, y: u32): u32` | RGBA `vec4<f32>` |
| Fragment grayscale | `export function shade(x: float, y: float): float` | clamped grayscale RGBA |

**Important:** The current Noqeri `float` type uses 64-bit host semantics,
while WebGPU consumes **f32**. The shader target explicitly narrows float
inputs, literals and arithmetic to f32. GPU computations will not be
bit-exact relative to the CPU on values near float32 precision limits.

Shader functions may use scalar-typed immutable locals, early returns,
`if/else`, boolean comparisons and logical operators, plus pure u32/float
arithmetic and explicit casts. Every branch must eventually return the
correct scalar type. Symbol names are remapped to generated WGSL identifiers
so source names such as `var` and `for` cannot conflict with WGSL keywords.

The shader code generator rejects pointers, memory indexing, imported host
variables, arbitrary calls, loops, mutable assignments, unsafe operations,
opaque captures, invalid casts, unbounded statement nesting, and literal-zero
division. This is **not** a complete Noqeri general-purpose execution target.
Division by a runtime-zero variable still follows GPU/WGSL semantics; callers
should guard that case explicitly.

## Compile commands

```sh
build/noqeri shader examples/shaders/color_gradient.noqeri build/gradient.wgsl --stage=fragment
build/noqeri shader examples/shaders/posterize_fragment.noqeri build/posterize.wgsl --stage=fragment
build/noqeri shader examples/shaders/grayscale_fragment.noqeri build/grayscale.wgsl --stage=fragment
build/noqeri shader examples/shaders/invert_compute.noqeri build/invert.wgsl --stage=compute
build/noqeri shader examples/shaders/float_compute.noqeri build/float-compute.wgsl --stage=compute --workgroup-size=128
```

Compute workgroup size defaults to 64, with allowed explicit values **32,
64, 128, 256**. Passing a workgroup-size flag to a fragment shader or
specifying unknown/duplicate flags fails. The generated WGSL declares its
actual `@workgroup_size`; the browser host reads it instead of assuming 64.

## WebGPU browser dispatch

Start a local server from the repository root:

```sh
python3 -m http.server 8000
```

Open `http://localhost:8000/examples/shaders/webgpu_demo.html`. The browser
demo allows choosing three fragment shaders and dispatches separate integer
and floating-point compute examples.

The JavaScript adapter `examples/shaders/webgpu_host.mjs` only owns
device, pipeline, buffer and presentation resources. It verifies that WGSL
storage element types match `Uint32Array` or `Float32Array`, validates
device buffer/workgroup limits and releases resources on error. A successful
fragment render returns an idempotent release function; the demo invokes it
when switching shaders and unloading.

WGSL is validated by Naga in GitHub CI across **five real Noqeri shader
sources**. Node mock-WebGPU tests exercise dispatch calculations and failure
cleanup. **These tests are not evidence of a physical GPU or a particular
browser/driver passing hardware execution.**

The Linux CPU/native SDL2 renderer remains a separate path. Graphics logic
is written in Noqeri in `Lib/std/render2d.noqeri` and
`Lib/std/os_framebuffer.noqeri`. Do not conflate SDL2 desktop framebuffer
presentation with the WebGPU compute/fragment pipeline.

## Outstanding engineering gates

General Noqeri shader features (textures, samplers, f32 vector/matrix types,
vertex shaders, storage address spaces, loops with bounded execution and
shader-oriented attributes), native SPIR-V/MSL/HLSL, and fully self-hosted
Stage-2 shader compiler parity are not yet implemented. Physical browser/GPU
driver qualification and real native macOS/Windows renderer deployment also
remain separate acceptance tests.
