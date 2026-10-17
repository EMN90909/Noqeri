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
