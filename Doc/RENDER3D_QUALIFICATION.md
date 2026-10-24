# Noqeri rendering verification and 3D profile (experimental)

The rendering work has **two separately qualified execution paths**:

1. Native Noqeri-authored **CPU raster** using `Lib/std/render2d.noqeri`
   and `Lib/std/render3d.noqeri`, with an optional SDL2 presentation bridge.
2. WebGPU 3D **shader and host subset**: a Noqeri-written vertex function
   compiled to WGSL, a Noqeri-written fragment shader, and JavaScript
   resource/pipeline setup through the browser's WebGPU API.

These are NOT interchangeable. A successful CPU differential test is
not evidence of GPU execution, and a Naga shader-validation pass is not
physical driver qualification.

## New native 3D functionality

`Lib/std/render3d.noqeri` exports:

| Function | Contract |
|---|---|
| `nqProjectVertex3D(x,y,z,width,height,out)` | Signed camera coordinates looking along +Z, integer perspective using focal = min(width,height)/2, output `[screenX, screenY, positiveZ]` |
| `nqDepthClear3D(depth,width,height)` | Initialize exactly the visible depth pixels to `0xffffffff`; preserve any tail |
| `nqRasterTriangle3D(pixels,depth,width,height,vertices,color)` | Solid-color triangle with signed pixel coordinates, barycentric interpolated integer depth, Z-buffer occlusion, both windings and bounding-box clipping |

The vertex slice stores three `[x,y,z]` triples (9 signed 64-bit
integers). The units of `z` are arbitrary but consistent: smaller is
nearer. The projection output is suitable for the raster call; it is
not a perspective-correct varying/attribute implementation.

**Safety invariants:**

- Width and height must be nonzero and at most 4096 each.
- Buffer lengths are checked with `height <= len / width` before
  computing `width*height`.
- Vertex slice length >= 9 and output projection length >= 3.
- Screen x/y in [-8192,8192], positive z in [1,65535].
- Camera x/y in [-262144,262144].
- Invalid calls must return `false` without modifying buffers.
- A degenerate triangle is a no-op success. Triangles outside the
  viewport have no effect.
- Signed edge products and z-weight interpolation are bounded by the
  input caps, and therefore fit i64.
- An existing depth value is replaced only when the new interpolated
  value is **strictly smaller**. Draw ordering resolves exact-depth ties.

The pixel loop uses incremental edge stepping and a precomputed
depth-numerator step; no per-pixel coordinate divisions or repeated
three-product barycentric calculations. Flat-depth triangles avoid
depth division entirely. This is an optimized scalar path, **not SIMD**.

The independent native C++ reference in
`tests/cpp/render3d_differential.cpp` computes raster coverage and
depth independently per pixel. CI compiles the Noqeri module through
the real x86-64 native compiler with `-O3` and checks projection,
occlusion, depth/pixel arrays, winding, clipping, out-of-domain
rejection, truncated slices and untouched tail guards.

To reproduce:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNOQERI_WARNINGS_AS_ERRORS=ON
cmake --build build --target noqeri -j2
bash tests/scripts/render3d-native-e2e.sh build/noqeri
```

The native sample is `examples/render3d_demo.noqeri`. It draws
overlapping triangles with different Z values and presents the finished
pixels using the narrow OS/SDL2 host ABI.

## Noqeri WebGPU vertex and depth path

The compiler's experimental `--stage=vertex` accepts exactly
`export function shade(value: vec4): vec4`. Its input is an
already-transformed homogeneous clip position `[x,y,z,w]`.
It emits `@vertex` with `@location(0) vec4<f32>` input and
`@builtin(position) vec4<f32>` output. A fragment shader
emits `@location(0) vec4<f32>` RGBA.

```sh
build/noqeri shader examples/shaders/vertex3d.noqeri build/vertex3d.wgsl --stage=vertex
build/noqeri shader examples/shaders/fragment3d.noqeri build/fragment3d.wgsl --stage=fragment
cargo run --quiet --manifest-path tests/wgsl-validator/Cargo.toml -- build/vertex3d.wgsl build/fragment3d.wgsl
python3 -m http.server 8000
```

Open `http://localhost:8000/examples/browser-webgpu/webgpu3d_demo.html` in
a secure-context WebGPU browser. `renderNoqeriMesh3D()` builds a
hardware depth-tested render pipeline with `depth24plus` and
`depthCompare: less`; it uploads bounded float32x4 vertex arrays and
draws complete triangle lists. The JavaScript host is resource/
lifecycle glue only, not the Noqeri shader implementation.

The host validates canvas size, input type/count, clip-space finiteness
and W/depth ranges; snapshots caller memory; checks device resource
limits and shader compilation messages; and destroys vertex buffers,
depth textures and the device on explicit release or failure. Mocked
device tests exercise these branches. CI independently validates
generated WGSL with Naga.

## Performance and qualification

The earlier **measured** native 2D 256×256 benchmark improved gradient
by 2.69× and checker by 2.41× over pinned Noqeri source on a matched
GitHub runner (see `Doc/CPU_RASTER_PERFORMANCE.md`). Clearing
remained essentially unchanged and slower than C. This new 3D pipeline
did not exist in the pinned benchmark; **no legitimate before/after 3D
speedup can be claimed from that dataset**.

A production 3D release still requires hardware/driver/WebGPU
qualification (including output pixel comparisons, device loss, resize,
timing, pacing and real GPU timestamps), plus broader engine features.
The following remain unsupported: vertex-indexed meshes, interpolated
vertex attributes, matrix/uniform transforms in the GPU shader,
arbitrary shaders, backface configuration, texture UV interpolation,
near-plane polygon clipping, a watertight top-left triangle fill rule,
perspective-correct varying interpolation, MSAA, skeletal animation,
resource streaming and physical Vulkan/Metal/D3D drivers.

The rasterizer's **fixed bounded profile** is appropriate for
controlled demos and differential testing. Do not treat it as a
general-purpose production 3D game engine. Stage-2 self-hosted WGSL
generation also remains unqualified; these WGSL stages are generated
by the Stage-0 C++ emitter.

## First 3D CPU performance evidence (2026-10-24)

GitHub Actions [run 40121020892](https://github.com/EMN90909/Noqeri/actions/runs/40121020892)
passed native 3D differential qualification and archived 30 raw samples
per engine and depth scene. The independent C++ reference recomputes
barycentric pixel coverage across the full surface; it is **not** an
identical implementation of the Noqeri optimized incremental algorithm.

| 256×256 | Noqeri median | Independent C++ median | Noqeri/C++ time ratio |
|---|---:|---:|---:|
| Flat depth | 0.334 ms | 0.093 ms | 3.60× |
| Interpolated depth | 0.349 ms | 0.092 ms | 3.79× |

No earlier Noqeri 3D CPU implementation was measured in the uploaded
performance CSV, so these numbers establish a performance baseline
rather than a historical improvement. No real GPU shader timestamps
or physical-device performance were measured.

## Multi-frame WebGPU resource reuse

For interactive 3D loops, use
`createNoqeriMeshRenderer3D(canvas, vertexWGSL, fragmentWGSL, gpu?, maxVertices?)`
to compile once and reuse the WebGPU device, pipeline, vertex buffer
and depth attachment. The returned object has `await renderer.render(vertices)`
and `renderer.dispose()`. The default capacity is 4095 vertices and
triangle counts must be a multiple of three.

The host rejects canvas resize and concurrent in-flight submissions,
snapshots and validates caller arrays, and destroys resources after
device errors. The single-frame helper remains for one-off renders but
incurs shader and pipeline setup costs on every invocation.

This architecture removes repeated GPU initialization from subsequent
frames. **No numerical GPU speedup is claimed** until real device
timestamps and browser/driver results are collected.

## Native executable-stack hardening

The Linux ELF x86-64 Stage-0 native emitter now writes
`.note.GNU-stack` with non-executable permissions, so linked
graphics executables no longer silently inherit an executable stack
from missing assembler metadata. The 2D and 3D native test harnesses
fail if `readelf -W -l` reports an executable `GNU_STACK` segment
or no such segment. This is an important defense-in-depth property,
not a proof that all native memory operations are memory-safe.

## Subsequent 3D and browser compatibility work

The edge interpolation path now advances its depth numerator along both
horizontal pixels and scanline rows, replacing three multiplications per
raster row with a precomputed signed step. The paired same-compiler
benchmark compares against the pinned `tests/fixtures/render3d_baseline.txt`
and rejects a greater-than-25% median slowdown.

The WebGPU host now offers
`inspectNoqeriGpuCompatibility(gpu?)` to report adapter features,
limits and preferred surface format **without allocating a device**.
The renderer tries a high-performance adapter and falls back to the
browser's default adapter when needed. Persistent rendering listens
to `GPUDevice.lost`, invalidates the previous resources, and requires
the caller to create a new renderer rather than trying to use a dead
pipeline. Per WebGPU's device model, lost-device resources must be
recreated. No vendor-specific GPU driver or command submission bypass
is involved.

For hands-on GPU qualification, compile these additional shader files:

```sh
build/noqeri shader examples/shaders/vector_compute.noqeri build/noqeri-vector-compute.wgsl --stage=compute
build/noqeri shader examples/shaders/texture_fragment.noqeri build/noqeri-texture-fragment.wgsl --stage=fragment
build/noqeri shader examples/shaders/vertex3d.noqeri build/noqeri-vertex3d.wgsl --stage=vertex
build/noqeri shader examples/shaders/fragment3d.noqeri build/noqeri-fragment3d.wgsl --stage=fragment
```

Run `examples/browser-webgpu/physical_qualification.html` from localhost or
HTTPS. The browser harness exercises vector compute, textured fragments
and a two-triangle 3D depth path while preserving results for manual
inspection. It deliberately reports `hardwareQualified: false`;
real driver certification still requires a trusted adapter/driver
inventory, framebuffer readback assertions, device-loss/resize testing
on named hardware and GPU timestamp evidence.

## 3D depth-clear optimization acceptance

`nqDepthClear3D` now uses four checked u32 stores per iteration and a
scalar remainder. This does not relax dimension validation and never
writes beyond the visible `width * height` pixels. The full-buffer
reference check includes truncated slices and untouched tail sentinels.

The independent 3D benchmark records three scenes: flat-depth triangles,
interpolated-depth triangles and depth-buffer clear at 256×256. Each
scene is tested with thirty native Noqeri and C++ samples. The A/B
comparison builds the pinned original `render3d.noqeri` with the
**same** compiler and reference driver as the updated source. CI gates
a median regression above 25%. This is native CPU performance evidence,
not GPU shader execution or frame rate qualification.

## New guarded-pointer 3D performance evidence

The rasterizer and depth clear now validate framebuffer slice lengths
before deriving native nonvolatile pointers, and then use those pointers
only for indexes proven below `width * height`. This removes dynamic
slice-header checks from the hot loop while retaining the C++ full-buffer
oracle and no-write invalid-input tests. External code must not mutate
or invalidate the buffers during rendering.

GitHub Actions [run 40127662918](https://github.com/EMN90909/Noqeri/actions/runs/40127662918)
passed native correctness, shader validation, the GPU firmware simulator
and a 30-sample same-host A/B comparison against the pinned previous
Noqeri source:

| 256×256 native CPU operation | Previous median | Candidate median | Speedup |
|---|---:|---:|---:|
| Flat-depth triangle | 0.470 ms | 0.424 ms | 1.109× |
| Interpolated-depth triangle | 0.455 ms | 0.433 ms | 1.053× |
| Clear Z-buffer | 0.129 ms | 0.103 ms | 1.254× |

The results reflect the complete candidate optimization relative to the
pinned Noqeri implementation, not individual instruction attribution.
CI runner results vary with host loading; speedups require continued
same-run regression monitoring. These are CPU benchmarks only and do not
qualify GPU/driver performance or physical firmware execution.

## Resize without rebuilding shader pipelines

The persistent WebGPU 3D renderer now exposes
`renderer.resize(width,height)` for canvas backing dimensions already
changed by the caller. It validates device limits and rejects concurrent
in-flight frames. It allocates the replacement `depth24plus` attachment
before destroying the previous attachment, retains shader and pipeline
objects, and permits subsequent frames without shader recompilation.
Invalid resize calls and normal resize cleanup have mock-GPU tests.
This avoids repeated pipeline setup when changing window resolution,
but does not itself qualify physical driver behavior or guarantee a
specific measured GPU speedup.


## Indexed WebGPU triangles and flat-depth specialization (October 2026)

The persistent `createNoqeriMeshRenderer3D(...)` API now supports
`renderIndexed(vertices, indices)` (waits for completion) and
`renderIndexedFast(vertices, indices)` (bounded queued submissions).

For shared-vertex meshes, supply one validated `Float32Array` of
homogeneous `vec4` clip positions, which does not need to contain a
multiple of three vertices. Supply `Uint16Array` or `Uint32Array`
indices in complete groups of three, all less than the vertex count.
The optional `{maxIndices: 12285}` capacity (default:
`maxVertices * 3`) must be a bounded multiple of three. The GPU
index buffer is allocated lazily and reused for all indexed draws;
the vertex buffer, render pipeline, depth and optional MSAA targets
are also reused. Both arrays are snapshotted and validated before
submitting commands. The classic `render`/`renderFast` triangle-list
API is unchanged. Disposal and device-loss cleanup release the index
buffer too.

For the native CPU path, flat-depth triangles now take a separate
pixel loop without depth numerator increments or integer depth
division. Interpolated depth keeps its original exact division and
strict depth comparison. The independent CPU full-frame differential
and native pinned-source benchmark must pass before performance
improvements may be claimed.

No actual indexed-mesh physical GPU FPS or real-driver performance
result is implied by JavaScript mock tests, and Stage-0 WGSL codegen
does not constitute a self-hosted shader compiler.


Optional browser-only examples have been moved to
`examples/browser-webgpu/` to isolate JavaScript from the Noqeri/C++
shader compiler and shader source tree. This does not imply that the
C++ compiler now contains a native WebGPU hardware driver.
