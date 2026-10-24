# Game development platform matrix

As of 2026-10-18, **no platform has a verified end-to-end Noqeri 3D gaming toolchain**.

| Platform | Compiler target policy | Native executable proof | SDL3 + GPU evidence | Full game |
|---|---|---|---|---|
| Linux x86-64 | Source present | Blocked | Not integrated | Not ready |
| Linux ARM64 | Source present | Unverified | Not integrated | Not ready |
| Windows x86-64 | Source present | Unverified | Not integrated | Not ready |
| Windows ARM64 | Source present | Unverified | Not integrated | Not ready |
| macOS x86-64 | Source present | Unverified | Not integrated | Not ready |
| macOS ARM64 | Source present | Unverified | Not integrated | Not ready |
| Browser | Restricted JS compiler | Not a native executable | WebGPU/WebGL game path unverified | Not ready |

A target policy, object writer source or host SDK installed on a developer computer is not proof that the Noqeri compiler generates a working application.

Per-platform acceptance evidence requires exact compiler and host versions, target triple, linker result, executable launch/exit results, graphics API/driver/GPU identification, input/resize/cleanup negative tests and CI or self-managed-runner artifacts.

## Experimental 3D subsystem update (2026-10-24)

`Lib/std/render3d.noqeri` provides bounded native CPU perspective
projection and integer depth-buffer triangle rasterization. A restricted
Noqeri vertex shader now emits WebGPU WGSL, and the browser host supports
a depth-tested triangle-list pipeline. See
[Render3D qualification](RENDER3D_QUALIFICATION.md) for exact tested
limits and negative cases.

**This does not change the full-game readiness verdict above.** No real
GPU driver, physical device, cross-platform executable, mature 3D asset
pipeline or complete 3D game loop is qualified by these tests.
