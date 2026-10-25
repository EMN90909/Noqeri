<p align="center">
  <img src="Brand/noqeri-logo.webp" width="210" alt="Noqeri logo">
</p>

<h1 align="center">Noqeri</h1>

<p align="center">
  <strong>A general-purpose language with simple syntax and explicit systems control.</strong><br>
  Statically typed. C++-bootstrapped. Growing toward a Noqeri-owned compiler.
</p>

<p align="center">
  <a href="https://github.com/EMN90909/Noqeri/blob/main/CHANGELOG.md"><img alt="Version 0.1.3 development" src="https://img.shields.io/badge/version-0.1.3%20development-F59E0B"></a>
  <a href="LICENSE"><img alt="GPL-3.0-only license" src="https://img.shields.io/badge/license-GPL--3.0--only-2563EB"></a>
  <a href="Doc/PRODUCTION_READINESS.md"><img alt="Experimental development" src="https://img.shields.io/badge/readiness-experimental-64748B"></a>
</p>

<p align="center">
  <a href="#quick-start">Quick start</a> ·
  <a href="#what-works-today">Capabilities</a> ·
  <a href="#toolchain-and-self-hosting">Toolchain</a> ·
  <a href="#documentation">Documentation</a> ·
  <a href="#project-status">Project status</a>
</p>

---

Noqeri is an evolving **statically typed, general-purpose programming language** developed by [Noethric](https://noethric.xyz). It combines inferred local variables and a compact application-oriented syntax with lower-level capabilities such as fixed-width integers, pointers, slices, atomics, native interfaces and explicit compiler targets.

This repository contains the working **C++17 Stage-0 compiler**, Noqeri-written compiler modules, the NQRI virtual machine, libraries, test infrastructure and experimental browser, graphics and distributed-computing subsystems. Their maturity is not uniform. **The current source version is `0.1.3`, a development version, not a production-qualified release.**

## A first look

```noqeri
function add(a: int, b: int): int {
    return a + b
}

let language = "Noqeri"

if language == "Noqeri" {
    print("Hello from Noqeri")
    print(add(10, 20))
}
```

Local values can infer their types, semicolons are optional, and `if` / `while` conditions do not require parentheses. Functions, fixed-size arrays, slices, records, generics and imports are part of the language surface. See the [language reference](Doc/LANGUAGE_REFERENCE.md) and the checked-in [hello-world example](examples/hello.noqeri).

## Platform builds and downloads

A [cross-platform distribution workflow](.github/workflows/distribution.yml) now
defines compiler SDK builds for **Android, Linux, Windows, macOS, FreeBSD and
OpenBSD**, including x86-64 and ARM64 variants. Windows archives use `.zip`;
other platforms use `.tar.gz`. Each package includes the Stage-0 compiler,
the NQRI VM, headers and Noqeri source libraries.

Run it via [GitHub Actions](https://github.com/EMN90909/Noqeri/actions/workflows/distribution.yml),
or use a [tagged GitHub Release](https://github.com/EMN90909/Noqeri/releases)
*after all target builds pass*. The existence of the workflow does **not**
certify those host platforms or native cross-target executable compilation.
See [build instructions and qualification status](Doc/DISTRIBUTION.md).

## Quick start

**Prerequisites:** Git, CMake **3.16+**, and a **C++17 compiler**. The following configuration builds the real Stage-0 `noqeri` CLI while omitting optional mailbox/PGAS services and their platform-specific dependencies.

**Linux / macOS**

```sh
git clone https://github.com/EMN90909/Noqeri.git
cd Noqeri

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNOQERI_GRAPHICS_PORTABLE_BUILD=ON
cmake --build build --config Release --target noqeri

./build/noqeri --version
./build/noqeri check examples/hello.noqeri
./build/noqeri run examples/hello.noqeri
```

**Windows (PowerShell, Visual Studio multi-config generator)**

```powershell
git clone https://github.com/EMN90909/Noqeri.git
cd Noqeri

cmake -S . -B build -DNOQERI_GRAPHICS_PORTABLE_BUILD=ON
cmake --build build --config Release --target noqeri

.\build\Release\noqeri.exe --version
.\build\Release\noqeri.exe check examples\hello.noqeri
.\build\Release\noqeri.exe run examples\hello.noqeri
```

For single-configuration generators, the executable is generally at `build/noqeri` (or `build/noqeri.exe`), rather than under `Release/`. This is a **source build**, not an installer or a signed binary release. The full default CMake configuration additionally builds experimental native components and, on Linux, requires **OpenSSL development libraries**. See [platform support](Doc/PLATFORM_SUPPORT.md) and [graphics build profiles](Doc/GRAPHICS_PLATFORM_SUPPORT.md).

### Try a project

```sh
./build/noqeri new demo
./build/noqeri check demo/src/main.noqeri
```

New projects use `project.nqon`, the portable **Noqeri Object Notation** manifest, rather than JSON or a JavaScript package manifest. For example:

```nqon
name = demo
version = "0.1.0"
```

NQON is a separate text data format, **not** the compiled bytecode format. Read [NQON's specification](Doc/NQON.md) or [Getting started](Doc/GETTING_STARTED.md).

## What works today

The table describes **code present in this repository**, not a guarantee of production or cross-platform certification.

| Area | Current implementation | Important boundary |
| :--- | :--- | :--- |
| **Language** | Static checking, inferred locals, functions, records, generics, arrays/slices, imports and low-level systems features | Some Stage-2 parity and release conformance gates remain open |
| **Stage-0 compiler** | C++17 frontend, typed NIR, optimizations, reference execution and freestanding x86-64 assembly generation | Native executable tooling and target support vary by platform |
| **NQRI bootstrap** | SHA-256-pinned Stage-1 seed; Stage-2/Stage-3 bytecode rebuild and byte-equality check through a C++ NQRI VM | **Not** native self-hosting or independent seed attestation |
| **Web development** | Experimental Noqeri views, components, reactivity and a restricted web-to-ES-module compiler | Browser runtime and host tooling still use JavaScript; no production SSR/hydration |
| **Graphics & shaders** | Noqeri-authored CPU raster modules, portable framebuffer fastpaths, and limited vertex/fragment/compute shaders compiled to WGSL | Stage-0 C++ shader backend; GPU drivers and full 3D engine are not qualified |
| **Packages & data** | Package/lockfile tooling, portable NQON, Registry API experiments and NoeSQL adapters | Canonical NoeSQL and Registry integration is not end-to-end production-verified |
| **Distributed computing** | Experimental TLS, WAL, bounded remote bytecode execution and optional Linux RDMA provider | Native distributed-address lowering, hardware tests and automatic safe failover remain incomplete |

### Useful compiler commands

These commands belong to the **Stage-0 `noqeri` executable** built above, not to the restricted self-hosted Stage-2 CLI.

| Command | What it does |
| :--- | :--- |
| `noqeri check file.noqeri` | Parse and type-check a source/module graph |
| `noqeri run file.noqeri` | Run through the reference execution path |
| `noqeri build file.noqeri output.s -O3` | Emit target assembly; **not** automatically a platform executable |
| `noqeri nqri file.noqeri module.nqri` | Emit portable NQRI bytecode |
| `noqeri new demo` | Create a starter project |
| `noqeri format file.noqeri` | Format source in place |
| `noqeri test tests` | Run the language test command against a directory |
| `noqeri shader file.noqeri output.wgsl --stage=fragment` | Compile the supported shader subset to WebGPU WGSL |

Other Stage-0 commands include `lex`, `nir`, `web`, `targets`, `manifest`, `lock`, `audit`, `doctor`, `lsp` and explicit assembler/linker adapters. Run `noqeri` without arguments to display the usage guide. The exact interfaces and supported feature sets are still evolving.

## Experimental web and graphics

<details>
<summary><strong>Browser UI built from Noqeri</strong></summary>

The checked-in web-view example uses `view` blocks, reactive events, local UI components and a small browser DOM runtime. Build it with **Node.js 22+**:

```sh
node Runtime/web-project.mjs examples/web_view dist-web
node --test tests/web-views.test.mjs tests/web-project.test.mjs tests/web-compiler.test.mjs
```

The builder generates HTML and ES modules; it does **not** turn the browser into a JavaScript-free runtime. See [Web Views](Doc/WEB_VIEWS_V1.md), [the example](examples/web_view/main.noqeri), and [web implementation status](Doc/WEB_PROJECT_STATUS.md).

</details>

<details>
<summary><strong>CPU rendering and WebGPU shaders</strong></summary>

`Lib/std/render2d.noqeri` and `Lib/std/render3d.noqeri` contain CPU raster routines with separate native verification fixtures. The optional SDL2 bridge presents rendered frames. The Stage-0 shader backend converts a **restricted** Noqeri shader profile to WGSL:

```sh
./build/noqeri shader examples/shaders/vector_fragment.noqeri build/vector_fragment.wgsl --stage=fragment
```

WGSL compilation does not establish physical GPU compatibility. Browser WebGPU demonstrations also use JavaScript for device and resource setup. Read [shader profile v3](Doc/GPU_SHADER_V3.md), [3D qualification](Doc/RENDER3D_QUALIFICATION.md), and [the platform matrix](Doc/GRAPHICS_PLATFORM_SUPPORT.md).

</details>

<details>
<summary><strong>PGAS and native runtime experiments</strong></summary>

The repository includes experimental partition annotations, transport/security primitives, WAL-backed state, remote bytecode and an optional `libibverbs` backend. The compiler **rejects executing** partitioned programs through unsupported lowering paths rather than silently treating them as distributed. See [PGAS acceptance](Doc/PGAS_DISTRIBUTED_ACCEPTANCE.md) and [network qualification](Doc/PGAS_NETWORK_QUALIFICATION.md).

</details>

## Toolchain and self-hosting

```text
.noqeri source
    │
    ├── C++17 Stage-0 frontend → typed NIR → reference run / assembly / NQRI
    │
    ├── experimental web compiler → browser ES modules
    └── experimental shader backend → WGSL

Compiler/selfhost/*.noqeri
    └── pinned Stage-1 NQRI → C++ NQRI VM
          └── Stage-2 NQRI → Stage-3 NQRI (byte-equality verification)
```

There are **two different build paths**:

1. **`cmake --build build --target noqeri`** builds today's runnable **C++ Stage-0 CLI**.
2. **`./bootstrap.sh`** (or `.\bootstrap.ps1` on Windows) verifies the **pinned NQRI self-rebuild**. It builds or locates the VM, verifies the Stage-1 SHA-256 pin and compares Stage-2 and Stage-3 bytecode. It does **not** replace the Stage-0 CLI with a proven native self-hosted compiler.

The historical Stage-1 seed predates the `0.1.3` version alignment; its pinned provenance is not the version of the currently shipped CLI. Noqeri's object-format modules and native-code self-hosting work still require validation. See [bootstrap ownership](Compiler/selfhost/BOOTSTRAP.md), [self-hosting model](Doc/SELF_HOSTING.md) and [native targets](Doc/NATIVE_TARGETS.md).

## Repository layout

```text
Compiler/       Stage-0 implementation and Noqeri self-hosted compiler sources
Runtime/        NQRI VM, host adapters, browser tooling and runtime experiments
Lib/            Noqeri standard and optional game/security libraries
Include/        Compiler, runtime and embedding interfaces
Grammar/        Language grammar
examples/       Language, web, rendering and shader examples
Doc/            Specifications, qualification status and guides
tests/          Compiler, conformance, platform and integration tests
scripts/        Build, verification and benchmarking tools
Brand/          Project artwork
```

**File extensions:** `.noqeri` is source, `.nqri` is portable compiler bytecode, and `.nqon` is Noqeri Object Notation. Older `.nqr` and `.nqo` names are legacy migration formats, not the canonical names for new projects.

## Testing and release evidence

For the compiler-focused portable build:

```sh
ctest --test-dir build -C Release --output-on-failure
```

The full build has additional native test targets and prerequisites. To run the larger release gate, use Node.js and a complete checkout:

```sh
node scripts/qualify-release.mjs
```

**A failing release gate is meaningful.** It can identify unavailable suites, missing external qualifications or native self-hosting gaps; a passing unit test alone is not evidence of a stable release. Benchmarks similarly must distinguish interpreted/NQRI execution from [verified Linux x86-64 native AOT execution](Benchmarks/METHODOLOGY.md).

## Project status

**Development version: `0.1.3`.** The version is declared by `CMakeLists.txt`, `Include/noqeri/version.hpp` and `project.nqon`. It is **not** a published `1.0` stability claim.

Current priorities include native Stage-2 compiler parity, broader conformance and portability evidence, qualified GPU/graphics execution, secure distributed recovery and completed Registry/NoeSQL integration. The repository contains working components and meaningful tests, but that is **not equivalent to a production-certified toolchain**.

Follow [the changelog](CHANGELOG.md), [production-readiness criteria](Doc/PRODUCTION_READINESS.md) and [the roadmap](Doc/ROADMAP.md). For separate ecosystem projects, see [NoeSQL](https://github.com/EMN90909/NoeSQL), [Noqeri Registry](https://github.com/EMN90909/noqeri-registry) and [Noqeri Web](https://github.com/EMN90909/noqeri-web).

## Documentation

- **Start here:** [Getting started](Doc/GETTING_STARTED.md) · [Beginner course](Doc/BEGINNER_COURSE.md) · [Language reference](Doc/LANGUAGE_REFERENCE.md)
- **Compiler:** [Toolchain](Doc/TOOLCHAIN.md) · [Self-hosting](Doc/SELF_HOSTING.md) · [Optimizer qualification](Doc/OPTIMIZATION_QUALIFICATION.md)
- **Libraries and targets:** [ABI](Doc/ABI.md) · [Platform support](Doc/PLATFORM_SUPPORT.md) · [FFI](Doc/FFI.md)
- **Application areas:** [Web views](Doc/WEB_VIEWS_V1.md) · [Graphics](Doc/RENDER3D_QUALIFICATION.md) · [PGAS](Doc/PGAS_DISTRIBUTED_ACCEPTANCE.md) · [NQON](Doc/NQON.md)

## Contributing and license

Contributions and bug reports are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md), [the code of conduct](CODE_OF_CONDUCT.md) and [SECURITY.md](SECURITY.md) before reporting security-sensitive issues.

Noqeri is licensed under **GPL-3.0-only**, except where a component explicitly provides different terms. See [LICENSE](LICENSE) and [LICENSES.md](LICENSES.md) for details.

<p align="center">Built by <a href="https://noethric.xyz"><strong>Noethric</strong></a>.</p>
