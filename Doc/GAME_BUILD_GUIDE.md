# Build and test the game SDK

## Requirements

- Node.js 20+ for optional asset tooling.
- A **trusted, functional Noqeri Stage-0 compiler** and working Stage-2 build for language conformance.
- Hardware with a real OpenGL context for eventual 3D rendering. This repository does not yet provide a verified native gaming runtime.

## What works without a Noqeri compiler

Run the limited position-only OBJ reader and its Node tests:

```sh
node --test tests/game-obj.test.mjs
node Tools/game/validate-obj.mjs examples/game_sdk/assets/cube.obj --json
```

This verifies asset validation only. It does not compile the game SDK.

## Compile and check native Noqeri source

```sh
NOQERI_STAGE0=/absolute/path/to/trusted/noqeri ./scripts/build.sh
./build/noqeri check tests/game_foundations.nqr
```

The modern Stage-2 CLI may not support `run`. To execute the fixture, a verified runner with `run <source>` support is required:

```sh
/path/to/verified-noqeri-runner run tests/game_foundations.nqr
```

The fixture prints `0` only if its runtime assertions pass. A successful `check` alone is not a game-functionality proof.

## General-purpose language versus game SDK qualification

`node scripts/qualify-release.mjs` is the general-purpose 0.7.0 release qualification runner. It tests the optional asset tooling while keeping native 3D GPU certification separate. General-purpose release remains blocked until full bootstrap, native/runtime conformance, distribution and regression suites pass.

`node scripts/check-game-sdk.mjs` provides the stricter optional **game-development** qualification that also requires a real native runner and GPU rendering evidence. See `Doc/GAME_DEV_READINESS.md` and `Doc/RELEASE_VERIFICATION_2026-10-18.md`.

Do not replace a failed compiler with the restricted browser compiler or unverified C++ fallback. Store exact run commands and machine hardware in release evidence.
