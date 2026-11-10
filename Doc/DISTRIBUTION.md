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
| Package manager | share/noqeri/Tools/package/manager.noqeri and native compiler ABI |
| Build tools | bin/noqeri-web, compiler build/assemble/link tools, include/noqeri headers and native ABI libraries |

The registry uses a Noqeri-authored frontend and native libcurl/zlib
bootstrap services, with no Node.js runtime. Online package installation
requires a deployed Registry v1 endpoint and native transport providers.
Native executable creation via `noqeri build-exe` currently
uses GCC/Clang and the supported portable-C subset. Host architecture support
must not be confused with target code-generator support.

## Platform matrix and artifact names

| Host | CPU variants | Portable SDK archive | Native installer |
| --- | --- | --- | --- |
| Linux | x86, x86_64, aarch64 | .tar.gz | .deb, .rpm |
| Windows | x86, x86_64, aarch64 | .zip | NSIS setup .exe; MSI for x86/x86_64 |
| macOS | x86_64, aarch64 | .tar.gz | .pkg |
| Android/Termux | armv7, x86, x86_64, aarch64 | .tar.gz | Termux .deb |
| FreeBSD | x86_64, aarch64 | .tar.gz | .pkg |
| OpenBSD | x86_64, aarch64 | .tar.gz | pkg_add .pkg.tgz |

Examples (VERSION is the CMake source version):

- noqeri-sdk-vVERSION-linux-x86_64.tar.gz
- noqeri-sdk-vVERSION-windows-x86_64.zip
- noqeri-sdk-VERSION-linux-x86_64.deb
- noqeri-sdk-VERSION-windows-x86_64-setup.exe
- noqeri-sdk-VERSION-android-armv7-termux.deb
- noqeri-sdk-VERSION-freebsd-x86_64.pkg
- noqeri-sdk-VERSION-openbsd-x86_64.pkg.tgz

New archives have a single `noqeri-sdk-vVERSION-OS-CPU/` root and contain
all five components. System-native installers install those same SDK files.
The **noqeri-VERSION-android-universal-debug.apk** remains an additional
separately signed editor/debug app rather than a complete CLI SDK.

Only download a target after it appears in
https://github.com/EMN90909/Noqeri/releases with SHA256SUMS and
source-commit provenance. The distribution jobs are **build pipelines**, not
evidence of successful execution on all 16 platforms. Some NDK targets are
cross-compiled, and physical device execution is not yet qualified.

## Installation examples

On Debian/Ubuntu, download the matching SDK package, then:

    sudo apt install ./noqeri-sdk-VERSION-linux-x86_64.deb
    noqeri --version
    noqeri pkg help

On Fedora/RHEL:

    sudo dnf install ./noqeri-sdk-VERSION-linux-x86_64.rpm

On macOS:

    sudo installer -pkg noqeri-sdk-VERSION-macos-aarch64.pkg -target /
    /usr/local/bin/noqeri --version

On FreeBSD:

    sudo pkg add ./noqeri-sdk-VERSION-freebsd-x86_64.pkg

On OpenBSD:

    doas pkg_add ./noqeri-sdk-VERSION-openbsd-x86_64.pkg.tgz

On Termux, use the SDK package for the device ABI:

    dpkg -i noqeri-sdk-VERSION-android-aarch64-termux.deb

On Windows, run the appropriate **SDK** setup .exe or MSI. For portable
archives, extract and add the resulting SDK bin directory to PATH; install
no Node.js runtime is necessary for registry package operations.

## Build a complete SDK locally

Requires a C++17 compiler and CMake >=3.16:

    cmake -S . -B build -DNOQERI_GRAPHICS_PORTABLE_BUILD=ON -DNOQERI_ENABLE_SDL2=OFF -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release --target noqeri noqeri-nqri noqeri-web noqeri_abi noqeri_owned_collections noqeri_graphics_fastpath
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
