# Noqeri installers, 32-bit builds and binary downloads

The **Noqeri cross-platform distributions** workflow
(`.github/workflows/distribution.yml`) builds the Stage-0 compiler,
the portable NQRI virtual machine and Noqeri SDK sources, then packages
the *same staged files* into host-specific installers, archives and directly
downloadable native executables.

This is an **implemented CI pipeline**, not proof that every platform
has passed qualification. Always inspect the Actions results for each target.

## Supported build configurations

| Operating system | Architecture | Portable SDK | Native installer |
| --- | --- | --- | --- |
| Linux | x86-64, AArch64 | `.tar.gz`, `.bin` | `.deb`, `.rpm` |
| Linux | 32-bit x86 (i386) | `.tar.gz`, `.bin` | `.deb`, `.rpm` |
| Windows | x86-64 | `.zip`, `.exe` | NSIS setup `.exe`, `.msi` |
| Windows | 32-bit x86 | `.zip`, `.exe` | NSIS setup `.exe`, `.msi` |
| Windows | ARM64 | `.zip`, `.exe` | NSIS setup `.exe` |
| macOS | Intel x86-64, Apple Silicon | `.tar.gz`, `.bin` | `.pkg` |
| Android | ARM64, x86-64 | `.tar.gz`, `.bin` | Termux `.deb` |
| Android | 32-bit ARMv7, x86 | `.tar.gz`, `.bin` | Termux `.deb` |
| Android | four Android ABIs | N/A | Universal debug-signed `.apk` |
| FreeBSD | x86-64, AArch64 | `.tar.gz`, `.bin` | FreeBSD `.pkg` |
| OpenBSD | x86-64, AArch64 | `.tar.gz`, `.bin` | OpenBSD `.pkg.tgz` (pkg_add) |

**macOS does not run 32-bit user applications on modern releases.**
A 32-bit macOS installer is therefore not offered. The BSD VM provider
currently qualifies only its 64-bit x86 and AArch64 guests; i386
packages are not silently repackaged from 64-bit builds. No claim is
made that every physical GPU/driver is qualified.

On 32-bit hosts, the delivered compiler is a 32-bit *host executable*,
but Noqeri's native code generator and ABI have not been converted
to a general 32-bit machine-code backend. NQRI and the reference
interpreter are separate from the native backend.

## Package naming and file types

For a source version `0.1.3` and matching git tag `v0.1.3`,
examples of GitHub release assets are:

- `noqeri-v0.1.3-linux-x86.tar.gz`: 32-bit Linux portable SDK
- `noqeri-v0.1.3-linux-x86.bin`: native Linux i386 compiler executable
- `noqeri-nqri-v0.1.3-linux-x86.bin`: native i386 NQRI VM
- `noqeri-0.1.3-linux-x86.deb`: native Debian-family installer
- `noqeri-0.1.3-linux-x86.rpm`: native RPM-family installer
- `noqeri-v0.1.3-windows-x86.exe`: 32-bit Windows compiler executable
- `noqeri-0.1.3-windows-x86-setup.exe`: Windows installer, not the compiler
- `noqeri-0.1.3-windows-x86.msi`: Windows Installer package
- `noqeri-0.1.3-macos-aarch64.pkg`: macOS installer
- `noqeri-0.1.3-android-armv7-termux.deb`: Termux package for 32-bit ARM
- `noqeri-0.1.3-android-universal-debug.apk`: four-ABI sideloadable app
- `noqeri-0.1.3-freebsd-x86_64.pkg`: FreeBSD pkg(8) package
- `noqeri-0.1.3-openbsd-x86_64.pkg.tgz`: OpenBSD pkg_add(1) package

A standalone executable still needs the correct operating system
loader and system C/C++ libraries. **`.bin` is a filename extension,
not a universal binary format.** Unlike archive/installer builds,
the bare executable does not contain the standard library source tree.

## What gets installed

The CMake portable SDK includes:

- `bin/noqeri` or `bin/noqeri.exe` (Stage-0 compiler)
- `bin/noqeri-nqri` or `bin/noqeri-nqri.exe` (NQRI interpreter)
- `lib/` (portable graphics fastpath static library)
- `include/noqeri/` (C/C++ API headers)
- `share/noqeri/` (Noqeri language modules, runtime sources, branding,
  GPL-3.0 license)

On Linux packages install into `/usr`; macOS and FreeBSD use
`/usr/local`. Windows native installers use the Windows system
installer UI. OpenBSD pkg_add installs to `/usr/local`.

## Installation examples

**Debian/Ubuntu x86-64 or i386**

```sh
sudo apt install ./noqeri-0.1.3-linux-x86.deb
noqeri --version
```

**Fedora/RHEL RPM family**

```sh
sudo dnf install ./noqeri-0.1.3-linux-x86_64.rpm
noqeri --version
```

**macOS**

```sh
sudo installer -pkg noqeri-0.1.3-macos-aarch64.pkg -target /
/usr/local/bin/noqeri --version
```

**FreeBSD**

```sh
sudo pkg add ./noqeri-0.1.3-freebsd-x86_64.pkg
/usr/local/bin/noqeri --version
```

**OpenBSD**

```sh
doas pkg_add ./noqeri-0.1.3-openbsd-x86_64.pkg.tgz
/usr/local/bin/noqeri --version
```

**Windows**: run the matching `-setup.exe` or `.msi`.
The standalone `noqeri-v...-windows-*.exe` is the compiler, **not**
an installer. 32-bit Windows uses the x86 build; x86-64 uses x64.

**Android/Termux**: use an appropriate Android ABI package in
the Termux shell:

```sh
dpkg -i noqeri-0.1.3-android-aarch64-termux.deb
noqeri --version
```

The Termux package is installed under the default
`/data/data/com.termux/files/usr` prefix; other Android terminals
or an OEM system shell are not interchangeable with Termux.

**Android APK**: AndroidApp embeds the Stage-0 compiler and interpreter
via JNI. It offers an on-device Noqeri source editor with
**Check** and **Run** controls. The APK contains Android native
libraries for `armeabi-v7a`, `arm64-v8a`, `x86`, `x86_64` and uses
a development/debug signing key. This is not Play Store ready; signing
identity changes across CI runs, which may require uninstalling an old
debug APK before installing the next one. Physical emulator/device
runtime qualification is pending. It does not replace the Termux CLI.

## Building locally

Requires a C++17 compiler and CMake 3.16+:

```sh
cmake -S . -B build -DNOQERI_GRAPHICS_PORTABLE_BUILD=ON \
  -DNOQERI_ENABLE_SDL2=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build --target noqeri noqeri-nqri noqeri_graphics_fastpath
cmake --install build --prefix stage
```

Native CPack installers depend on OS packaging tools. After a
successful host build, for example:

```sh
cpack --config build/CPackConfig.cmake -G DEB -B artifacts
cpack --config build/CPackConfig.cmake -G RPM -B artifacts
```

On Windows use NSIS or WIX, on macOS use `productbuild`, and on
FreeBSD use `FREEBSD`. The Android APK can be built from
`AndroidApp/` using AGP 8.9.2, Gradle 8.11.1 and NDK r27c,
provided Android SDK 35 and CMake 3.22.1 are installed.

For a Linux i386 cross-build on a Linux x86-64 host, install multilib
headers/toolchain, and configure with
`-DCMAKE_CXX_FLAGS=-m32 -DCMAKE_EXE_LINKER_FLAGS=-m32`.
For Windows x86 use Visual Studio's `-A Win32` generator
(ARM64 requires the corresponding VS ARM64 workload).
Android 32-bit ABIs are `armeabi-v7a` and `x86`.

## Release gates and security

1. A workflow_dispatch run generates CI artifacts without publishing.
2. For a release, update the CMake project version and Android app
   versionName together, and tag that commit `v<version>`.
3. All 16 OS/CPU command-line matrix jobs must pass. The APK must
   compile and contain all four JNI ABIs.
4. Each native installer and directly downloadable executable must be
   nonempty. A missing installer makes the release fail, not silently
   fall back to an archive.
5. SHA-256 checksums are calculated and verified before GitHub Release.

For Linux/Android, CI also checks ELF machine/bitness; Windows checks
PE machine identifiers; native BSD executables run basic smoke tests;
native desktop builds run graphics portability tests.

**Unsigned release packages:** Windows installers are not Authenticode
signed, and macOS `.pkg` files are not Apple notarized. Android APKs
are debug-signed, not production signed. SHA-256 is for transfer integrity,
not publisher authenticity. Before a public stable release, production
signing keys, notarization and a device/emulator acceptance suite are needed.

Finally, `noqeri build foo.noqeri foo.s` emits assembly for the
compiler's supported target ABI; it does **not** imply that the host
compiler can link `foo.noqeri` into a native executable for all
16 release platforms. Native target code generation, cross-linking
and GPU driver qualification are separate engineering gates.
