<p align="center"><img src="Brand/noqeri-logo.webp" alt="Noqeri code logo" width="560"></p>
<h1 align="center">Noqeri</h1>

**Version on `main`: 0.7.0 (development, not production-qualified).** The `1.0.0` proposal is **not** a published stable compiler version. `CMakeLists.txt`, `Include/noqeri/version.hpp`, and `Compiler/selfhost/driver.noqeri` still report 0.7.0 on this branch. Do not claim `v1.0.0` solely from documentation. Release readiness requires native Stage-2 parity, reviewed bootstrap provenance, a passing platform matrix, and actual NoeSQL/Registry cross-repository tests.
<p align="center"><strong>A general-purpose, statically typed language moving to a Noqeri-owned self-hosted compiler.</strong></p>

Noqeri active source files use `.noqeri`, project metadata uses portable `project.nqon`, and native portable bytecode uses `.nqri`. **NoeSQL** is the canonical planned database engine with `.noedb` storage. Old NoqeriDB/NoqeriSQL host code and `.nqd/.nqdb` formats remain compatibility/migration surfaces, **not** a second recommended database platform. Noqeri main is still version **0.7.0**; a v1.0.0 stabilization candidate exists but native cross-repository release qualification is incomplete.

## Reactive web development (experimental, implemented on `main`)

Build a simple application in **one Noqeri source** with markup, reactive state, components, optional CSS, JavaScript interop and local Noqeri UI packages:

```noqeri
let count = 0
view {
    h1 { "Hello from Noqeri" }
    button on.click { count = count + 1 } { "Count: " + count }
}
```

```sh
node Runtime/web-project.mjs examples/web_view dist-web
node --test tests/web-views.test.mjs tests/web-project.test.mjs tests/web-compiler.test.mjs
```

The web builder creates a minimal `index.html`, ES modules and a browser DOM runtime. `import js "gsap" as gsap` requires an explicit local ES module or import map, while `import ui/badge as ui` resolves a local `ui/badge.noqeri` package. See [Web Views](Doc/WEB_VIEWS_V1.md).

**Important:** This is a separately tested restricted web-view compiler, **not** the typed self-hosted Stage-2 NIR backend, and not production-qualified SSR, hydration, routing, or a universal JavaScript replacement. Existing HTML/CSS/JS web projects continue to build through the original compatibility path.

## Database and Registry integration

The private NoeSQL repository has migrated 21 language sources to `.noqeri`, and the public Registry repository now has a NoeSQL-backed HTTP v1 service. Those repositories **do not yet provide a proven production-ready native compiler/database integration**. The default CLI still uses the historical GitHub-content adapter. `Runtime/package-manager-v1.mjs` provides the experimental Registry v1 API client, but enabling that as default before deploying the NoeSQL-backed service would break existing package installs. Both paths must not be described as one production-integrated implementation. Do not label a changed protocol file as a completed production migration.

## Migration status (verified on `main`, October 21, 2026)

| Repository | `.noqeri` | `.nqri` | `.nqr` | `.nqo` |
| --- | ---: | ---: | ---: | ---: |
| Noqeri | 320 | 1 | 0 | 0 |
| NoeSQL | 21 | 0 | 0 | 0 |
| Registry | 8 | 0 | 425 | 0 |
| Web | 3 | 0 | 3 | 2 |

Registry's 425 `.nqr` paths are historical package release contents under `packages/`, not active Registry app source. Web's three `.nqr` and two `.nqo` files are segregated under `tests/fixtures/legacy/`. These are **not** equivalent to active compiler source needing in-place rename. Mutating immutable release bytes would change checksums and break pinned consumers; publish migrated package versions instead.

Noqeri still contains deprecated NoqeriDB/NoqeriSQL compatibility code in `Database/` and `Compiler/bootstrap/noqeridb_host.cpp`. **NoeSQL is canonical**, but the old runtime has not been fully removed or bridged to the qualified native NoeSQL engine. Registry `runtime/server.mjs` / `runtime/catalog.mjs` use the `.noedb` reference path, while `src/registry.noqeri` is still a native host shim. Web chooses the Registry HTTP API only when `NOQERI_REGISTRY_API_URL` is configured, otherwise using a legacy GitHub source adapter. These are live migration boundaries, not a completed end-to-end production deployment.

See [web view docs](Doc/WEB_VIEWS_V1.md), [self-hosting](Doc/SELF_HOSTING.md), and [database history](Database/README.md).

## Beginner surface

A single file needs no project ceremony:

```noqeri
let name = "Noqeri"

if name == "Noqeri" {
    print("Hello")
}
```

Types are inferred for initialized local values, semicolons are optional, and condition parentheses are unnecessary. `noqeri new demo` creates a two-line `project.nqon` plus a one-line starter program. Explicit types remain available where they communicate an API or systems boundary.

```nqon
name = demo
version = "0.1.0"
```

NQON is specified independently of the compiler and can be converted to/from JSON with the standalone reference tooling. See [NQON](Doc/NQON.md) and the [syntax simplicity contract](Doc/SYNTAX_SIMPLICITY.md).

## Compiler architecture

`main` now uses a staged self-host model. The normal build no longer invokes CMake or a C++17 compiler. Compiler rules being migrated live in `Compiler/selfhost/*.noqeri`; the first stage owns lexical scanning, structural syntax checks, primitive type-width/sign policy, token counting and source fingerprinting.

The C++17 Stage-0 bootstrap remains a provenance/seed dependency, not a separately verified production implementation. Historical bootstrap branches may exist but are not a release-certification mechanism. See [`Doc/SELF_HOSTING.md`](Doc/SELF_HOSTING.md) for the exact trust boundary and remaining parity work.

## Build locally

A fresh machine can build a C++ Stage-0 compiler to produce a local Stage-1 seed candidate. This enables bootstrapping, but it is not independently attested seed provenance.

Linux/macOS/POSIX:

```sh
./bootstrap.sh
```

Windows PowerShell:

```powershell
.\bootstrap.ps1
```

A fresh checkout builds the audited C++ Stage-0 bootstrap compiler and the native `noqeri-nqri` runtime, emits `build/noqeri-stage1.nqri`, then uses that Noqeri compiler artifact to build Stage 2 and Stage 3. The bootstrap succeeds only when the Stage-2 and Stage-3 `.nqri` files are byte-identical. An externally audited Stage-1 binary can be supplied with both `NOQERI_STAGE1_NQRI` and its independently checked `NOQERI_STAGE1_SHA256`. No independently trusted Stage-1 seed is currently committed; Stage-2/Stage-3 byte equality does not eliminate the C++/native-host trust boundary.

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
