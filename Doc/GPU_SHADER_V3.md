# Noqeri GPU shader profile v3: vectors and sampled RGBA textures

This profile is **experimental**. Noqeri source is parsed by the shared
Stage-0 parser and typechecked by the bounded shader-specific C++ backend.
The output is WebGPU WGSL, not a native GPU driver or SPIR-V binary.

## Supported Noqeri shader declarations

- Scalar types: `u32`, `float` (narrowed to f32), `bool`.
- GPU vector types: `vec2`, `vec3`, `vec4` (all f32).
- Vector constructors `vecN(float,...)` and scalar splat `vecN(float)`.
- Vector swizzles `.x`, `.y`, `.xy`, `.xyz`, `.xyzw` with
  bounds checks. Invalid components are rejected by the compiler.
- Vector arithmetic `+`, `-`, `*`, `/` on matching widths.
- Builtins: `dot`, `length`, `normalize`, `clamp`,
  `sampleTexture(uv: vec2)`, `textureSize()`.
- Fragment shaders: `shade(x: float, y: float): vec4` for RGBA output,
  in addition to v2 packed RGB/grayscale signatures.
- Compute shaders: `shade(value: vec4): vec4`, with storage buffers
  holding `vec4<f32>` elements, in addition to v2 scalar signatures.
- Texture bindings use `@group(0) @binding(0)` for
  `texture_2d<f32>` and `@group(0) @binding(1)` for the sampler.
  Sampling uses `textureSampleLevel(..., 0.0)` so the generated code
  remains valid in potentially non-uniform control flow. It does not
  provide implicit-derivative mipmapping or arbitrary mip selection.

Identifiers are remapped and arbitrary calls, pointers, writes, host captures,
loops, unbounded expressions, unsafe operations, and bad casts are rejected.
The shader type checker is deliberately distinct from CPU/native typing:
the `check` command does not qualify shader-only vector programs.

## Compile shaders

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target noqeri -j 2

build/noqeri shader examples/shaders/vector_fragment.noqeri build/noqeri-vector-fragment.wgsl --stage=fragment
build/noqeri shader examples/shaders/texture_fragment.noqeri build/noqeri-texture-fragment.wgsl --stage=fragment
build/noqeri shader examples/shaders/vector_compute.noqeri build/noqeri-vector-compute.wgsl --stage=compute
```

Validate generated WGSL with Naga:

```sh
cargo run --quiet --manifest-path tests/wgsl-validator/Cargo.toml -- build/noqeri-vector-fragment.wgsl build/noqeri-texture-fragment.wgsl build/noqeri-vector-compute.wgsl
```

The WebGPU host `renderNoqeriFragment(canvas, wgsl, gpu, rgbaTexture)`
takes a texture descriptor `{width, height, data: Uint8Array}` with exactly
four bytes per pixel. The host enforces checked dimensions, WebGPU device
limits and destroys buffers/textures/devices after use or error.

The compute host accepts `Float32Array` for vec4 compute, with exactly
four floats per element. Workgroup dispatch counts elements rather than
individual float components.

## Physical GPU-driver qualification

After generating WGSL in `build/`, serve the repository root:

```sh
python3 -m http.server 8000
```

Open `http://localhost:8000/examples/shaders/physical_qualification.html`
in a browser with hardware-backed WebGPU. Click **Run actual GPU tests**.
The runner exercises a **real** vector-compute pipeline, checks eight
f32 output components, and compiles, binds, uploads and executes a
textured fragment draw. It records GPU-adapter details when the browser
discloses them. Review the displayed pixels and preserve the output JSON.

**Hardware qualification is NOT complete based on CI or this harness existing.**
A browser can expose software adapters; `isFallbackAdapter` is not
universally disclosed. A qualification record needs a named GPU model,
browser version, OS, driver version, screenshot, rendered-pixel validation,
driver failure/context loss tests and verified runs on target machines.
Mock tests and headless SDL2 are not substitute evidence.

## Stage-2 self-hosted shader parity is still a separate gate

The committed Stage-1/Stage-2 bootstrap produces NQRI bytecode for the general
compiler. It currently does **not** expose a Noqeri-written shader-to-WGSL
emitter. This profile is implemented in `Compiler/shader_backend.cpp`.
It must not be described as self-hosted.

The fail-closed parity runner can be invoked once an independently built
shader-capable Stage-2 compiler and its provenance record exist:

```sh
NOQERI_STAGE0_SHADER_COMPILER=build/noqeri \\
NOQERI_STAGE2_SHADER_COMPILER=/path/to/independent-stage2-shader-compiler \\
NOQERI_STAGE2_SHADER_PROOF=/path/to/verified-stage2-shader-proof.json \\
node scripts/qualify-shader-stage2.mjs
```

The runner checks both paths are distinct, checks the Stage-2 artifact's
SHA-256 against its proof, rejects unverified Stage-1 / unequal Stage-2 and
Stage-3 / Stage-0 delegation claims, then checks byte-identical output
from six shader fixtures. Its proof JSON must come from an independently
auditable build: self-asserted JSON is not independent provenance.

To qualify self-hosted shaders, implement shader parsing, typed vectors,
swizzles, texture resources, control-flow checks, and WGSL serialization
inside `Compiler/selfhost/*.noqeri`; compile that emitter through the
pinned Stage-1 -> Stage-2 -> Stage-3 chain; execute the Stage-2 compiler
without delegating shader code generation to the Stage-0 C++ host; compare
byte-identical WGSL across every positive fixture; reject all negative
fixtures identically; and run Naga validation on Stage-2 output.

## Remaining limitations

This v3 profile does **not** support matrix types, interpolated vertex inputs,
vertex shaders, multiple textures or sampler arrays, storage images, dynamic
resource layouts, derivatives, arbitrary memory access, bounded loops, full
GPU standard library, SPIR-V/MSL/HLSL output, or native driver integration.
It is a useful checked WebGPU subset, **not a complete general-purpose shader
language**.

## Bounded 3D vertex profile

The experimental `--stage=vertex` extension accepts one
`shade(value: vec4): vec4` function for **homogeneous clip positions**.
The 3D browser adapter builds an actual WebGPU depth24plus render
pipeline and accepts validated float32x4 triangle lists. The GPU vertex
source is still compiled by the Stage-0 shader emitter.

Refer to [Render3D qualification](RENDER3D_QUALIFICATION.md) for the
native Noqeri CPU triangle raster pipeline, depth semantics, WebGPU
host resource contracts, testing and device-qualification limitations.


## Shader math and code-generation improvements (October 2026)

The C++ Stage-0 emitter supports typechecked GPU-native floating-point
`cross(vec3,vec3)`, `reflect(vecN,vecN)`, `abs(float|vecN)`,
`min`/`max` (matching floats or vectors), and `mix` (three matching
float/vector types). Unsupported widths and mixed scalar/vector arguments
remain compile errors. Unary vector negation now preserves the vector's
type instead of incorrectly reporting `float`.

Packed RGB u32 fragments now emit one `vec3<f32>` conversion and vector
normalization rather than three independent scalar channel divisions.
GPU instruction scheduling remains driver-dependent; this is leaner
generated WGSL, **not a measured GPU performance claim**.

`examples/shaders/vector_lighting_fragment.noqeri` exercises typed 3D
vector lighting and is compiled and validated through the same Naga CI
gate as the other shader fixtures. Shader Stage-2 self-hosting and
physical GPU qualification are still not proven.
