# Noqeri SDK — developer preview

The Noqeri SDK is the installable distribution of the Noqeri language. Select an archive or native installer for your host operating system and CPU. Older v0.1.4 release assets are historical and are not retroactively repackaged.

## Included components

| Component | Installed location | Purpose |
| --- | --- | --- |
| Compiler and CLI | bin/noqeri (.exe on Windows) | Source checking, interpretation, compilation, assembly, linking and project scaffolding |
| Runtime | bin/noqeri-nqri plus share/noqeri/Runtime | NQRI bytecode execution and native runtime sources |
| Standard library | share/noqeri/Lib/std | Sources for strings, collections, files, HTTP and networking |
| Package manager | share/noqeri/Tools/package/manager.noqeri | Registry package addition, installation, resolution, vendoring and auditing |
| Build tools | bin/noqeri-web, CLI build and build-exe, include/noqeri | Native build tooling and experimental web compiler |

CMake installs all five components into the portable archives and system packages. The SDK does not claim that every standard-library module or native code-generation backend is production-complete.

## Install and first project

Archive names: noqeri-sdk-vVERSION-linux-x86_64.tar.gz, noqeri-sdk-vVERSION-windows-x86_64.zip. Native installers: noqeri-sdk-VERSION-linux-x86_64.deb, .rpm, etc. Extract an archive, put its bin directory on PATH, then run:

    noqeri --version
    noqeri new hello
    cd hello
    noqeri check src/main.noqeri
    noqeri run src/main.noqeri
    noqeri build-exe src/main.noqeri build/hello

Run the generated executable (on Windows, use build/hello.exe). The build-exe command uses the existing checked portable-C backend and a host GCC compiler by default. Add --backend=llvm to request Clang. Full-language and cross-CPU AOT support remains in development. You must have an appropriate host C compiler installed.

## Install libraries from noqeri-registry

    noqeri pkg add noqeri/components@0.2.1
    noqeri pkg install
    noqeri pkg resolve src/main.noqeri
    noqeri pkg vendor
    noqeri pkg audit

The SDK registry commands run through a **Noqeri-written command frontend** interpreted by the native compiler. Bounded HTTPS requests, SHA-256 validation, gzip/tar extraction, verified caching and filesystem updates are provided by C++17 bootstrap ABI services until those I/O primitives are self-hosted. No Node.js installation is needed for registry operations. Online package installation requires a compiler built with native libcurl and zlib; missing providers fail explicitly. Offline cached packages work without networking.

The registry backend is a separate Registry v1 service from the [noqeri-registry](https://github.com/EMN90909/noqeri-registry) repository. Set `NOQERI_REGISTRY_API_URL` to its deployed HTTPS origin (or loopback HTTP for local development). The CLI does **not** fetch unverified GitHub repository files as a fallback. Use `NOQERI_OFFLINE=1` to prohibit network access, or `noqeri pkg vendor --offline` for an already cached dependency graph. Package import resolution does not yet automatically link arbitrary third-party packages into all native backends.

Noqeri-UI supplies the noqeri/components library through the registry. Noqeriweb is the documentation/website repository, not a bundled language runtime. 

## Release verification

Each SDK archive must contain the compiler, VM, stdlib sources, package CLI and dependencies, web build tool, public headers and this guide. Release packaging rejects unsafe archive members, missing components, and inconsistent source commits. Checksums identify download contents but do not replace publisher signatures. Cross-built targets should be described as build-only until executed on their target machine. Android APK is a separate debug-signed editor app, not an SDK archive.

Read Doc/DISTRIBUTION.md for the platform matrix, installer commands and security limitations.
