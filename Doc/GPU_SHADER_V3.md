# Noqeri Shader v3: vector and texture GPU profile

This development branch adds genuine Noqeri-to-WGSL lowering for the
bounded GPU vector/texture profile. The general-purpose `noqeri`
parser still rejects shader-only intrinsics during CPU compilation.
The `shader` command deliberately uses the same Noqeri grammar with
a separate shader-specific type checker and emitter. Unsupported CPU
uses must **not** silently substitute the shader implementation.

## Noqeri types and intrinsics

- `Vec2`, `Vec3`, `Vec4` represent 2, 3 and 4 component `f32`
  WGSL vectors. Constructors `vec2(float,float)`,
  `vec3(float,float,float)`, `vec4(float,float,float,float)`.
- Vector `+`, `-`, `*`, `/` are componentwise with matching
  dimensions. `.x`, `.y`, `.z`, `.w` read valid components.
- Pure vector intrinsics: `dot`, `length`, `normalize`,
  `mix(v,v,t)` and `clamp(v,min,max)`, with closed type checking.
- `textureSample(uv: Vec2): Vec4` emits derivative-based WGSL
  sampling and is **fragment-only**. `textureSampleLevel(uv:Vec2,
  mipLevel:float):Vec4` emits an explicit-level sample usable from
  either compute or fragment.
- Texture declarations bind group 0 binding 0 `texture_2d<f32>`
  and binding 1 filtering sampler. Host supplies a real RGBA8 texture
  with explicit dimensions, validated byte count and padded upload
  pitch.
- Fragment shaders may return a `Vec4` RGBA color when their two
  positional inputs are floats. Generated code clamps all RGBA
  components to 0..1. Compute shaders may accept and return
  `Vec2`, `Vec3` or `Vec4` typed storage elements.
- WebGPU runtime buffer inputs are `Float32Array`, with storage
  stride 2 for Vec2 and 4 for Vec3 and Vec4. **Vec3 requires a fourth
  padding float per array element.** Storage-array layout is not the
  same as packed application-level RGB triplets.

The shader language does not yet support vector arrays within expressions,
matrices, writable textures, multiple samplers, vertex shaders, arbitrary
WGSL user-defined structs, multisampling, texture dimensions as typed
values or a general native SPIR-V/HLSL/MSL backend. Type checks reject
unsupported names and operations instead of trusting arbitrary WGSL input.

## Examples

```sh
build/noqeri shader examples/shaders/vector_mix_compute.noqeri build/vector.wgsl --stage=compute
build/noqeri shader examples/shaders/vector_fragment.noqeri build/vector-fragment.wgsl --stage=fragment
build/noqeri shader examples/shaders/texture_fragment.noqeri build/texture.wgsl --stage=fragment
```

The existing browser demo at
`examples/shaders/webgpu_demo.html` now also shows Vec4 compute,
vector-generated RGB and RGBA texture sampling.

## Stage-2 independently authored shader compilation

`Compiler/selfhost/shader_wgsl.noqeri` is a Noqeri-written WGSL
compiler that runs as **actual Stage-2 NQRI bytecode**. Its export
`stage2ShaderCompileWgsl(source,stage,workgroup,out)` is invoked by
`noqeri-nqri shader`, and the Stage-2 compiler module is rebuilt by
the pinned Stage-1 seed, then checked against Stage-3 for byte
identity.

**Stage-2 currently only accepts one exported `shade` function with
a pure u32 return expression, one u32 argument (compute) or two
u32 arguments (fragment), without locals or calls.** It independently
parses the source through the Noqeri-owned lexer, validates its scalar
expression subset and emits real WGSL. It rejects VecN, texture,
f32, arbitrary host calls and other unsupported expressions.

To compile via the self-hosted NQRI compiler once the bootstrap has
produced `build-selfhost/noqeri-stage2.nqri`:

```sh
build-native/noqeri-nqri shader build-selfhost/noqeri-stage2.nqri \
  examples/shaders/invert_compute.noqeri build/s2-invert.wgsl compute 128
build-native/noqeri-nqri shader build-selfhost/noqeri-stage2.nqri \
  examples/shaders/color_gradient.noqeri build/s2-gradient.wgsl fragment
```

This is a **tested real Stage-2 scalar shader compiler**, not
self-hosted parity for the full Stage-0 shader v3 profile. Real vector
and texture parity is still a remaining compiler milestone.

## Testing and acceptance

The GPU graphics workflow validates generated compute and fragment
WGSL through Naga, including sampled textures, vector constructors,
component reads and vector arithmetic, and runs browser-host mock
tests for stride, sampler attachment, bounds and cleanup. The Stage-2
bootstrap workflow compiles the scalar profile through the NQRI VM and
independently validates that WGSL. The Windows run checks that Stage-2
can compile its scalar program too. Neither is proof of real GPU
hardware execution.

Physical hardware qualification is a **separate manual workflow**
requiring a self-hosted runner with a non-fallback WebGPU-capable GPU.
See [GPU device qualification](GPU_DEVICE_QUALIFICATION.md).
