# Noqeri game SDK architecture (v0.7.0 development)

Noqeri remains a general-purpose language. Pure gameplay primitives live in `Lib/game/`; OS, graphics and audio are explicit versioned host boundaries. No GPU, SDL3 or renderer implementation is currently present or certified.

## Layers

1. Gameplay code and templates (`.nqr`).
2. Pure SDK math, fixed-timestep simulation, input action transitions and collision queries (`.nqr`).
3. Noqeri compiler, NIR, runtime and ownership checks.
4. Versioned native host ABI (existing ABI v2; native C compatibility **not yet proven**).
5. Future optional SDL3 window/event adapter and OpenGL 3.3 core renderer, followed by additional APIs only after conformance.

This preserves the language as reusable infrastructure rather than embedding a graphics engine into parser keywords.

## First rendered frame dependency graph

Trusted compiler seed -> Stage-2 equivalence -> Linux x86-64 executable -> stable scalar/aggregate FFI -> SDL3 create/destroy and event loop -> GL context and shader programs -> GPU buffers and indexed triangle -> textured rotating cube -> Noqeri-controlled camera/input and resource cleanup.

**Stop at the first failing gate.** Printing draw commands, opening an SDL window in a separate C++ app, or shipping a test-only JS renderer does not satisfy the Noqeri rendering gate.

## Native resource contract

- Create and destroy window/graphics/audio handles on their documented owning thread.
- Native allocations must have exactly one matching release operation.
- Expose opaque validated handles, not arbitrary live pointers, through public game bindings.
- Record ownership of GPU buffers, textures, shaders and draw context.
- Track context loss, resize, shader compilation failures, bounds, driver failures and teardown.
- Protect unsafe FFI operations within narrow host adapters.

Initial platform candidate: SDL3 (window/input) plus OpenGL 3.3 Core. SDL3 documentation: https://wiki.libsdl.org/SDL3/SDL_CreateWindow and https://wiki.libsdl.org/SDL3/SDL_GL_CreateContext. These are design references, **not installed dependencies or tested integration**.
