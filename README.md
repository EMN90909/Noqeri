<p align="center"><img src="Brand/noqeri-logo.webp" alt="Noqeri code logo" width="560"></p>
<h1 align="center">Noqeri</h1>

**Development version: 0.7.0 (unreleased, not qualified).** This is a development identity, not evidence of a stable compiler or functioning 3D game toolchain. Read [game development readiness](Doc/GAME_DEV_READINESS.md) and [release verification](Doc/RELEASE_VERIFICATION_2026-10-17.md) before using Noqeri in production.
<p align="center"><strong>A general-purpose, statically typed language moving to a Noqeri-owned self-hosted compiler.</strong></p>

Noqeri source uses `.noqeri`, portable project/data configuration uses NQON (`.nqon`), portable compiled objects use `.nqri`, and NoqeriDB uses `.nqd` / `.nqdb`. New projects use `project.nqon`; legacy `project.noqeri` manifests remain readable during migration.

## Compiler architecture

`main` now uses a staged self-host model. The normal build no longer invokes CMake or a C++17 compiler. Compiler rules being migrated live in `Compiler/selfhost/*.noqeri`; the first stage owns lexical scanning, structural syntax checks, primitive type-width/sign policy, token counting and source fingerprinting.

The last known-good C++17 bootstrap was preserved unchanged on branch **`bootstrap/cpp17-stage0`**. It is a provenance/seed compiler, not the production implementation path on `main`. See [`Doc/SELF_HOSTING.md`](Doc/SELF_HOSTING.md) for the exact trust boundary and remaining parity work.

## Build locally

A fresh machine needs one trusted Noqeri seed once—the same unavoidable bootstrap requirement used by self-hosting compilers generally.

Linux/macOS/POSIX:

```sh
./bootstrap.sh
```

Windows PowerShell:

```powershell
.\bootstrap.ps1
```

A fresh checkout builds the audited C++ Stage-0 bootstrap compiler and the native `noqeri-nqri` runtime, emits `build/noqeri-stage1.nqri`, then uses that Noqeri compiler artifact to build Stage 2 and Stage 3. The bootstrap succeeds only when the Stage-2 and Stage-3 `.nqri` files are byte-identical. An already verified Stage-1 seed can be supplied with `NOQERI_STAGE1_NQRI`.

Node.js is no longer part of the compiler self-rebuild path. JavaScript remains only in optional ecosystem/browser/reference tooling.

## Active Stage-2 CLI commands

```text
noqeri --version
noqeri check <file.noqeri>
noqeri lex-count <file.noqeri>
noqeri fingerprint <file.noqeri>
noqeri selftest
```

The historical compiler contains additional native/NIR/package/database/formatter/LSP commands. Those subsystems are deliberately not proxied through stage 1 until their Noqeri ports reach parity; silently falling back to C++ would defeat the dependency boundary.

## Beginner surface

Noqeri keeps the common path small while leaving explicit types available when they communicate a real constraint:

```nqr
let name = "Ada"

repeat 3 {
    print(name)
}

function double(value: int): int {
    return value * 2
}
```

Local bindings infer ordinary types, semicolons are optional, conditions do not need parentheses, top-level statements are allowed, and `repeat count { ... }` handles counted loops without a manual counter. The language does not add short aliases such as `fn` or a second import spelling merely to reduce character count; fewer concepts matter more than fewer keystrokes.

## NQON

NQON (Noqeri Object Notation) is a language-neutral configuration/data format. It does not require the Noqeri compiler:

```nqon
name = my-app
version = "1.0.0"

packages {
    forge = "noqeri/forge@1.2.0"
}
```

The standalone reference implementation can validate NQON and convert it to or from JSON:

```sh
node Runtime/nqon-cli.mjs check project.nqon
node Runtime/nqon-cli.mjs to-json project.nqon
```

See [the NQON specification](Grammar/nqon.grammar.md) and [NQON guide](Doc/NQON.md).

## Language capabilities already present

The current language supports fixed-width integers, arrays/slices, records, raw pointers, volatile memory, imports, register-value generics, checked status propagation, atomics, explicit casts, extern/export linkage, target intrinsics and constrained inline assembly. Reusable standard/security/test/database/platform functionality is already implemented under `.noqeri` directories.

## Optional game development SDK

Noqeri is a **general-purpose language**, not a game-specific language or a completed game engine. Optional, experimental source modules under `Lib/game/` provide vector math, bounded fixed-step simulation, button transitions and AABB collision queries. A Node.js OBJ asset validator is available for tooling. None of these features implies that native Noqeri game compilation, input/window adapters or GPU graphics are qualified. See [game SDK docs](Doc/GAME_SDK_API.md) and [the game readiness checklist](Doc/GAME_DEV_READINESS.md).

Use `node scripts/check-game-sdk.mjs --source-only` for a reproducible asset-tool smoke test. It is **not** a compiler test. The stable general-language release gate is independent of the optional full 3D-game certification.

## Registry and web distribution

- Packages: `EMN90909/noqeri-registry` — GPL-3.0-only Noqeri package implementations.
- Website/source bootstrap: `EMN90909/noqeri-web` — exposes POSIX and PowerShell source-install endpoints in addition to registry downloads.

Automatic GitHub Actions workflows are intentionally not required for this migration; local verification is the canonical path.


## Release readiness

**0.7.0 development, not verified stable:** the repository contains substantial implementations, but no proof of full self-hosting, native-backend parity or end-to-end browser parity. See [current verification and blockers](Doc/RELEASE_VERIFICATION_2026-10-18.md) and [the original compiler audit](Doc/RELEASE_VERIFICATION_2026-10-17.md). Full release qualification: `node scripts/qualify-release.mjs` with a trusted `NOQERI_STAGE0` seed and complete checkout.

## License

Noqeri's project code is distributed under the **GNU General Public License, version 3 only (`GPL-3.0-only`)**, unless a file or component carries a separate license notice. `LICENSE` contains the complete license text. See [`LICENSES.md`](LICENSES.md) for the project licensing guide and third-party component guidance.

Made by [Noethric](https://noethric.xyz).
