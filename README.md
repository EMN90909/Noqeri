<p align="center"><img src="Brand/noqeri-logo.webp" alt="Noqeri code logo" width="560"></p>
<h1 align="center">Noqeri</h1>
<p align="center"><strong>A compact statically typed language moving to a Noqeri-owned self-hosted compiler.</strong></p>

Noqeri source uses `.nqr`, project manifests use `project.nqr`, portable compiled objects use `.nqo`, and NoqeriDB uses `.nqd` / `.nqdb`.

## Compiler architecture

`main` now uses a staged self-host model. The normal build no longer invokes CMake or a C++17 compiler. Compiler rules being migrated live in `Compiler/selfhost/*.nqr`; the first stage owns lexical scanning, structural syntax checks, primitive type-width/sign policy, token counting and source fingerprinting.

The last known-good C++17 bootstrap was preserved unchanged on branch **`bootstrap/cpp17-stage0`**. It is a provenance/seed compiler, not the production implementation path on `main`. See [`Doc/SELF_HOSTING.md`](Doc/SELF_HOSTING.md) for the exact trust boundary and remaining parity work.

## Build locally

A fresh machine needs one trusted Noqeri seed once—the same unavoidable bootstrap requirement used by self-hosting compilers generally.

Linux/macOS/POSIX:

```sh
NOQERI_STAGE0=/path/to/noqeri ./scripts/build.sh
./build/noqeri selftest
./build/noqeri check examples/hello.nqr
```

Windows PowerShell:

```powershell
$env:NOQERI_STAGE0 = 'C:\path\to\noqeri.exe'
.\scripts\build.ps1
.\build\noqeri.cmd selftest
```

The build emits a portable `build/noqeri-stage1.nqo` and a platform launcher. The reference portable host requires Node.js 20+; the `.nqo` compiler kernel itself is OS-neutral.

## Current stage-1 commands

```text
noqeri --version
noqeri check <file.nqr>
noqeri lex-count <file.nqr>
noqeri fingerprint <file.nqr>
noqeri selftest
```

The historical compiler contains additional native/NIR/package/database/formatter/LSP commands. Those subsystems are deliberately not proxied through stage 1 until their Noqeri ports reach parity; silently falling back to C++ would defeat the dependency boundary.

## Language capabilities already present

The current language supports fixed-width integers, arrays/slices, records, raw pointers, volatile memory, imports, register-value generics, checked status propagation, atomics, explicit casts, extern/export linkage, target intrinsics and constrained inline assembly. Reusable standard/security/test/database/platform functionality is already implemented under `.nqr` directories.

## Registry and web distribution

- Packages: `EMN90909/noqeri-registry` — GPL-3.0-only Noqeri package implementations.
- Website/source bootstrap: `EMN90909/noqeri-web` — exposes POSIX and PowerShell source-install endpoints in addition to registry downloads.

Automatic GitHub Actions workflows are intentionally not required for this migration; local verification is the canonical path.

## License

Noqeri's project code is distributed under the **GNU General Public License, version 3 only (`GPL-3.0-only`)**, unless a file or component carries a separate license notice. `LICENSE` contains the complete license text. See [`LICENSES.md`](LICENSES.md) for the project licensing guide and third-party component guidance.

Made by [Noethric](https://noethric.xyz).
