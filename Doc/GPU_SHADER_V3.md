# Noqeri GPU v3: vectors, sampled textures and hardware acceptance

The Noqeri shader compiler recognizes GPU-specific `vec2`, `vec3` and
`vec4` value types, plus `u32`, `float` (narrowed to WGSL `f32`) and
`bool`. This uses Noqeri's canonical lexer/parser with a dedicated,
fail-closed **shader type checker**; GPU resources are not incorrectly
pretended to be host-side C++ structs.

## Supported GPU operations

- `vec2(float,float)`, `vec3(float,float,float)`,
  `vec4(float,float,float,float)`
- Same-dimension vector `+`, `-`, `*`, `/`, vector times float,
  vector divided by float, and float times vector
- `.x/.y/.z/.w` and `.r/.g/.b/.a` scalar component access, with
  dimension validation
- `dot(v,v)`, `distance(v,v)`, `normalize(v)`, `length(v)`,
  `mix(v,v,t)` and `clamp(a,min,max)`
- `sampleTexture(uv: vec2): vec4`, **fragment only**, with real WGSL
  sampled `texture_2d<f32>` and filtering sampler at group 0 bindings 0/1
- `shade(value: vec4): vec4` **compute**, using aligned
  `array<vec4<f32>>` storage buffers
- `shade(x: float, y: float): vec4` **fragment**, returning physical
  RGBA `vec4<f32>`

All calculations belong in `.noqeri` source. The WebGPU JS adapter only
owns resource upload, binding, execution, presentation and cleanup.

Shaders remain bounded, with no unrestricted calls, FFI, pointer access,
loops or arbitrary host state. Multicomponent swizzles, integer vectors,
custom resources and uniform layouts are not yet supported.

### Compile examples

```sh
build/noqeri shader examples/shaders/vector_compute.noqeri build/vector-compute.wgsl --stage=compute
build/noqeri shader examples/shaders/textured_fragment.noqeri build/textured-fragment.wgsl --stage=fragment
build/noqeri shader examples/shaders/vector_math_fragment.noqeri build/vector-math.wgsl --stage=fragment
```

The browser demo `examples/shaders/webgpu_demo.html` now offers sampled
textures, vector math, integer compute, float compute and vec4 compute.
For texture sampling, the host uploads an RGBA8 `Uint8Array`, validates
width/height and GPU texture limits, aligns row pitch, binds a filterable
sampler, and disposes the texture/device when no longer needed. For vec4
compute, the host requires `Float32Array` values in groups of four and
dispatches based on **vector count** rather than float count.

All eight Noqeri shader sources are compiled to WGSL and validated with
Naga in ordinary GitHub CI. Browser API behaviors are tested with mocked
GPU resources. Those tests **do not prove hardware execution**.

## Stage-2 language-owned backend

`Compiler/selfhost/shader_wgsl.noqeri` contains a real shader emitter
written entirely in Noqeri, using the same Noqeri-owned AST parser family
as the portable self-host compiler. It currently accepts a deliberately
restricted pure single-return `u32` or `float` compute function and
writes WGSL directly into a bounded caller-supplied buffer. The
`tests/shader_stage2_native.noqeri` acceptance fixture compiles that
Noqeri-written emitter into an executable and validates the generated WGSL.

**Full Stage-2 parity is not yet achieved**: the trusted pinned Stage-1
compiler has not been regenerated, Stage-2 CLI integration is not complete,
and the Noqeri-written emitter does not yet support all vector/texture and
control-flow features of the C++ Stage-0 target. Do not claim this subset
proves the full Stage-2 shader compiler or native self-hosting.

## Physical GPU qualification

The workflow `.github/workflows/gpu-hardware-qualification.yml` is
**manual and opt-in**. It requires a self-hosted Linux x86-64 runner with an
actual GPU, a hardware WebGPU-capable Chromium binary, and the label
`noqeri-physical-gpu`. Configure repository variable
`NOQERI_GPU_CHROMIUM` with the executable path on that runner.

The workflow builds Noqeri, compiles five shaders and launches Chromium
against the local demo. It rejects WebGPU fallback adapters and known
software-renderer identifiers, checks physical compute outputs for u32,
float and vec4, tests a texture-backed fragment, and saves an adapter
report and screenshot as CI artifacts. The GPU runner requires PCI display
enumeration through `lspci`, working Vulkan or other WebGPU device
drivers, Node, CMake, Clang/GCC, Python and npm network access.

**There is no qualified physical GPU result until that job actually passes.**
A passing job qualifies only its recorded device, browser and driver.
Other GPUs, platforms, power-management conditions, device loss/recovery
and performance regressions require separate coverage. Standard GitHub
hosted CI must never mark this gate passed by using SwiftShader,
Lavapipe, llvmpipe or another software renderer.
