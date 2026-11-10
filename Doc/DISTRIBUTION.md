# Noqeri SDK: download and installation

All **new Noqeri release artifacts** are complete host-specific SDKs rather
than loose compiler/VM executables. The existing historical v0.1.4 releases
are not retroactively modified.

Read [the SDK guide](SDK.md) for commands, five required components, registry
integration, and limitations. The release pipeline builds and checks real
installed files; a proposed platform matrix does not prove that an SDK is
published or qualified.

## Included SDK components

| Component | Installed files |
| --- | --- |
| Compiler/CLI | bin/noqeri (Windows: noqeri.exe) |
| Execution runtime | bin/noqeri-nqri, share/noqeri/Runtime and lib/runtime libraries |
| Standard library | share/noqeri/Lib/std string, file, http and other .noqeri modules |
| Package manager | bin/noqeri-pkg and native HTTPS/SHA-256 registry support |
| Build tools | bin/noqeri-web, compiler build/assemble/link tools, include/noqeri headers and native ABI libraries |

Package management uses the SDK's **native noqeri-pkg** executable. No Node.js installation is required. Online access needs a build with libcurl and OpenSSL; builds missing either dependency support offline-only mode.

## Build a complete SDK locally

Requires a C++17 compiler and CMake >=3.16:

    cmake -S . -B build -DNOQERI_GRAPHICS_PORTABLE_BUILD=ON -DNOQERI_ENABLE_SDL2=OFF -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release --target noqeri noqeri-nqri noqeri-web noqeri-pkg noqeri_abi noqeri_owned_collections noqeri_graphics_fastpath
    cmake --install build --config Release --prefix stage

The packaging utility `scripts/distribution/archive-sdk.sh` accepts
`NOQERI_RELEASE_TARGET=linux-x86_64`, stages from `stage/`, and **fails
closed if the compiler, VM, stdlib, registry package client, web tool or SDK
documentation is missing**. `scripts/distribution/repackage_release.py`
validates real host-native archives and emits reproducible, checksum-verified
SDK downloads. CPack creates installable DEB/RPM/NSIS/MSI/PKG packages.

## Release safety and qualification

The release gates check archive structure, manifest source revision, package
checksums, missing SDK components and unsafe archive entries. SDK archives
contain Noqeri source code and compiler/runtime tools, not claimed universal
static binaries. The 32-bit host compiler does not imply a 32-bit AOT backend.

Windows and macOS installers are not production-signed/notarized; Android APK
is debug-signed, and device acceptance tests are still outstanding. Verify
checksums, source provenance and the exact host OS/CPU before installing.

### Historical release v0.1.4

The existing tag
https://github.com/EMN90909/Noqeri/releases/tag/v0.1.4
keeps its older compiler/binary naming; its files are not rewritten.
The historical platform attach job is pinned to the original tag commit
and cannot attach new SDK-format assets from later main-branch builds.
