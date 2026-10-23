# Noqeri GPU shader v3: vectors, 2D textures and Stage-2 verification

The shader source-of-truth remains `.noqeri`. Stage-0 emits WebGPU WGSL;
the GPU driver, WebGPU adapter and device belong to the host, not to a
custom Noqeri graphics driver.

## Noqeri vector type and intrinsic library

Import `Lib/std/gpu_shaders.noqeri` into a shader:

```noqeri
import "../../Lib/std/gpu_shaders.noqeri"

export function shade(x: float, y: float): NqVec4 {
    const dimensions: NqVec2 = nqTextureSize()
    const uv: NqVec2 = nqVec2(x / dimensions.x, y / dimensions.y)
    return nqTextureSample(uv)
}
```

Canonical shader-only records `NqVec2`, `NqVec3`, and `NqVec4` all
contain 32-bit WGSL float components. The Noqeri source type `float`
is currently 64-bit in the general-purpose CPU toolchain and narrows to
f32 at the GPU boundary. These vector records are a **shader-specific ABI**:
their CPU memory layout is not ABI-compatible with a GPU packed vec3/vec4.
Do not pass their raw CPU record pointers to WGSL storage bindings.

Supported operations are typed constructors, addition and subtraction of
equal-length vectors, vector-by-f32 scalar multiplication, dot product,
length, normalization, vec4 mixing and clamp, component extraction,
and `x`/`y`/`z`/`w` member access where valid. The compiler
verifies exact record shapes and external intrinsic signatures and emits
approved WGSL expressions instead of calling native code.

General-purpose Noqeri operators on vector records, GPU matrix types,
swizzle combinations, f16 vectors and vertex shaders are not included.
Arbitrary host extern functions, malformed vector layouts, and
incorrect intrinsic argument types fail compilation.

## 2D fragment textures

The shader-stage-only intrinsic contract defines:

- `nqTextureSample(uv: NqVec2): NqVec4` for a normalized coordinate sample.
- `nqTextureLoad(x: u32, y: u32): NqVec4` for integer texel reads.
- `nqTextureSize(): NqVec2` for the current texture dimensions.

Texture image data is provided by the WebGPU host at group 0 binding 0.
Sampled shaders additionally require the filtering sampler at binding 1.
The shader compiler emits those bindings **only when used**.
All texture operations are restricted to fragment shaders; attempted
compute-texture calls are rejected rather than silently dispatched.

`examples/shaders/webgpu_host.mjs` binds a caller-owned RGBA8 byte array
with width/height metadata, pads texture upload rows to 256 bytes,
enforces device limits, creates the appropriate sampler, and disposes
textures and device resources. It dispatches `NqVec4` compute shaders
from `Float32Array` buffers where each four consecutive floats form one
GPU vec4. Input length must be divisible by four.

## Build and run

```sh
build/noqeri shader examples/shaders/vector_gradient_fragment.noqeri build/vector.wgsl --stage=fragment
build/noqeri shader examples/shaders/texture_fragment.noqeri build/texture.wgsl --stage=fragment
build/noqeri shader examples/shaders/texture_load_fragment.noqeri build/texture-load.wgsl --stage=fragment
build/noqeri shader examples/shaders/vector_compute.noqeri build/vector-compute.wgsl --stage=compute --workgroup-size=128
python3 -m http.server 8000
```

Also compile the original `gradient.wgsl`, `posterize.wgsl`,
`grayscale.wgsl`, `invert.wgsl`, and `float-compute.wgsl` described
in [Shader v2](GPU_SHADER_V2.md). Open
`http://localhost:8000/examples/shaders/webgpu_demo.html` and select the
fragment shader or compute example.

The GPU shader CI job validates the generated vector and texture WGSL
with Naga, including negative typechecker/intrinsic ABI tests and mocked
browser/WebGPU resource validation.

## Stage-2 self-hosted shader compiler addon

`Compiler/shaders/stage2_wgsl.noqeri` is a Noqeri-written code generator.
The **actual Stage-2 NQRI compiler** builds it from the separate
`stage2-shader-sources.txt` closure. The native NQRI VM's `shader`
command loads and executes that Noqeri-compiled generator and writes
WGSL through a bounded output slice.

The bootstrap CI requires byte-for-byte WGSL parity against Stage-0
for the pure unsigned scalar compute and fragment shader examples.
Incorrect inputs must be rejected before writing a shader output file.
It retains the repository's immutable Stage-1 seed; no hash-pinned
compiler source or historical binary is rewritten.

**Stage-2 support remains narrower than Stage-0.** This addon currently
supports one pure scalar `u32` return expression with one compute parameter
or two fragment parameters, and only `+`, `-`, `*`, `/`, `%` and
`as u32`. Stage-2 vector, texture, conditional, and float parity are
still not implemented. The shader addon executes under `noqeri-nqri`,
not directly as a native machine-code self-hosted shader compiler.

This distinction is deliberate: an addon compiled with Stage-2 is real
Noqeri-owned shader codegen, but it does **not** by itself certify general
self-hosted compiler independence from the NQRI VM.

## Physical GPU-driver qualification

`.github/workflows/gpu-hardware-qualification.yml` is a manual
hardware-only workflow. It requires a self-hosted Linux runner labeled
`gpu` and `noqeri-webgpu` with a real GPU device node and browser-ready
WebGPU hardware. It checks PCI render-controller inventory and attempts
to reject known software backends or fallback adapters.

The test then runs a genuine WebGPU browser, compiles Noqeri-generated
WGSL, dispatches integer, float and vec4 compute shaders, performs
sampled 2D rendering, and reads back 2D texture pixels from a physical
GPU render target. A successful run uploads
`webgpu-hardware-proof.json` and hardware inventory with the commit,
adapter details and evidence. **Hardware qualification is pending
until this workflow succeeds on a verified physical GPU runner.**

Standard GitHub `ubuntu-latest` CPU CI, Naga validation, browser mocks,
SDL2 dummy presentation, and software renderers are not substitutes for
that hardware evidence.

## Future general-purpose graphics engineering

Remaining unqualified: unified self-hosted vector/texture compiler
parity, texture arrays/depth/samplers outside this 2D profile, full
vector/matrix shader type system, vertex/geometry stages, direct
SPIR-V/HLSL/MSL generation, multi-vendor GPU driver matrix, device
loss recovery and repeatable throughput/performance baselines.
