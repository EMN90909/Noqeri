# Noqeri game-development workstream and v0.7.0 release gates

**Status: 0.7.0 development-version source committed; NOT a verified v0.7.0 release.**

Baseline: `main` tree `145667eb0bd8bc1b1e04aaf34c7ab8a412bd2f8a` (inspected 2026-10-18).
Reference target: Linux x86-64. First rendering path: SDL3 window/input and OpenGL 3.3 Core, subject to native ABI and driver proof.

## Strategy and scope

Noqeri remains a general-purpose statically typed language. Game capabilities are libraries built on top of an actually verified compiler/runtime, not a second language or an untested built-in engine. Deliver the smallest useful game workflow first: `create -> build -> run -> inspect failure`.

ThoughtfulBits-style plan review: the roadmap has a clear feature inventory and technical gates, but has no demonstrated user demand, baseline activation metric, design-partner validation, first-run experience measurements or real hardware evidence. Treat these as *unvalidated*, not as already shipped.

Product Design input for developer experience: the first user should encounter one documented project template, one canonical build command, explicit error messages and a visible rendered scene. A multi-screen editor is intentionally deferred.

## Priority checklist (checked only with executable evidence)

### P0: native language foundation
- [ ] Trusted seed builds a complete Stage-1 compiler.
- [ ] Stage-1 builds Stage-2, Stage-2 rebuilds compiler, outputs compare.
- [ ] Float/record/pointer/generic NIR lowering passes positive and negative conformance.
- [ ] Native x86-64 ELF executable links and runs on Linux.
- [ ] Native FFI calls external C library with verified ABI and correct cleanup.
- [ ] Existing language, browser, NoeSQL and package suites pass unchanged.

### P0: first 3D frame
- [ ] SDL3 window creates, pumps events, resizes and destroys cleanly.
- [ ] OpenGL backend compiles shaders, allocates buffers, draws and swaps.
- [ ] Noqeri application renders rotating triangle, then textured cube.
- [ ] GPU vendor, driver, hardware, build command and result recorded.
- [ ] Missing driver / malformed shader / input / cleanup negative cases pass.

### P1: reusable game library
- [ ] Vec2/3/4, Mat3/4, quaternions, projections and transformation reference tests.
- [ ] Monotonic fixed step, frame clamp, max catch-up and interpolation tests.
- [ ] Input abstraction, camera, mesh/texture assets and resource lifetime tests.
- [ ] Scene/entity lifecycle, collision/ray queries and audio playback.
- [ ] Playable 3D sample with reproducible assets and keyboard/mouse controls.

### P2: maturity
- [ ] Real hardware frame-time and memory regression measurements.
- [ ] Windows and macOS native ABI and graphics execution proofs.
- [ ] Animation, advanced materials, debugging and profiling.
- [ ] Multiplayer, editor and alternate renderer only after core proof.

## Initial implementation

- `Lib/game/math/vec3.noqeri`: pure `float` vector operations, cross/dot, normalization and interpolation.
- `Lib/game/core/fixed_step.noqeri`: bounded deterministic step planning, dropped-time accounting and render alpha.
- `Lib/game/physics/aabb.noqeri`: AABB overlap, point and ray tests.
- `Lib/game/input/button.noqeri`: backend-independent button transitions.
- `Tools/game/validate-obj.mjs`: bounded position-only OBJ vertex/face loading and triangulation.
- `tests/game_foundations.noqeri`: Noqeri native language conformance fixture, execution unverified.
- `tests/game-obj.test.mjs`: Node executable geometry validation tests.
- `scripts/check-game-sdk.mjs`: source-only test mode and fail-closed native/GPU release checks.

These are *source implementations awaiting compiler and execution verification*. No graphics API, hardware acceleration, or completed compiler bootstrap is implied. Before marking these ready: compile on a verified compiler, add reference-value tests (cross, dot, interpolation, zero and large clocks), run invalid-input paths, and record output. In particular review unsigned overflow semantics for nanosecond counters and use compiler-verified limits.

## General-language stability versus optional game-development readiness

**Noqeri 0.7.0 is a general-purpose language version.** Its stable release requires verified compiler bootstrap, usable native execution, type/memory/optimizer correctness, regression suites, distribution evidence and platform-specific tests as specified in [the general-purpose qualification report](RELEASE_VERIFICATION_2026-10-18.md). The language **does not require a complete 3D game engine** to be useful or to reach general-purpose stability.

The **separate 3D-game-ready claim** requires a real Noqeri executable to open a window, render a textured rotating cube, handle keyboard/mouse input and exit cleanly. Do not advertise that claim until native graphics/input, tests and GPU-driver evidence exist.

Game modules are optional libraries, not compiler keywords, runtime requirements or a restriction on use in web, database, CLI or systems development.

The original local evidence: `node --test tests/game-obj.test.mjs` passed 4/4 cases; `node Tools/game/validate-obj.mjs examples/game_sdk/assets/cube.obj --json` returned 8 vertices and 12 triangles; `node scripts/check-game-sdk.mjs --source-only` passed asset checks. The default full game gate exited 1 because the compiler, Noqeri runner and GPU proof were unavailable. These tests ran on a locally reconstructed, matching subset of the committed files, **not** a complete repository checkout. No successful native Noqeri build was performed: GitHub DNS access for checkout failed, and trusted bootstrap proof remains unavailable. Code review alone is not a release gate.

## Product metrics

Measure successful first-run rate, time to first displayed frame, and 95th-percentile frame time on a named reference machine. Baselines are unknown until tests run. Collect developer feedback on setup failures before expanding feature scope.

## Additional documents

- [Engineering strategy review](GAME_PRODUCT_PLAN_0_7.md)
- [Layered architecture](GAME_ARCHITECTURE.md)
- [SDK API contract](GAME_SDK_API.md)
- [Build and testing guide](GAME_BUILD_GUIDE.md)
- [Platform matrix](GAME_PLATFORM_MATRIX.md)
- [Performance evidence policy](GAME_PERFORMANCE.md)

## Follow-up verification (2026-10-18)

OBJ validator regression testing now includes rejection of concave, non-planar and self-intersecting faces, malformed limits and convex polygon triangulation. The focused asset/release scripts reported 8/8 tests passing on the local preview subset. Native Noqeri compilation and GPU rendering are still not verified. See [current release verification](RELEASE_VERIFICATION_2026-10-18.md) for exact scope and blockers.
