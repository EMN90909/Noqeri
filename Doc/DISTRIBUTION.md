# Noqeri cross-platform toolchain builds

`.github/workflows/distribution.yml` builds the **portable Stage-0 compiler SDK**
for Android, Linux, Windows, macOS, FreeBSD and OpenBSD. The workflow supports
manual runs from the Actions tab and tag-triggered GitHub Releases.

**This is a CI build matrix, not a claim that all jobs are passing yet.** Check
the workflow's individual job results before describing a target as qualified.

## Targets and produced files

| Platform | CPU / ABI | Builder | Archive |
| --- | --- | --- | --- |
| Linux | x86-64 | Ubuntu 24.04 x64 | tar.gz |
| Linux | AArch64 | Ubuntu 24.04 ARM64 | tar.gz |
| Windows | x86-64 | Windows Server 2022 | zip |
| Windows | AArch64 | Windows 11 ARM | zip |
| macOS | x86-64 | macOS 15 Intel | tar.gz |
| macOS | AArch64 | macOS 15 Apple Silicon | tar.gz |
| Android | arm64-v8a | Android NDK r27c, API 24 | tar.gz |
| Android | x86_64 | Android NDK r27c, API 24 | tar.gz |
| FreeBSD | x86-64 | FreeBSD 14.3 x64 VM | tar.gz |
| FreeBSD | AArch64 | FreeBSD 14.3 ARM64 VM | tar.gz |
| OpenBSD | x86-64 | OpenBSD 7.8 x64 VM | tar.gz |
| OpenBSD | AArch64 | OpenBSD 7.8 ARM64 VM | tar.gz |

The Android versions are **command-line toolchains**, not APK installers or
Android Studio apps. Use an Android app-private executable directory or an
appropriately configured terminal environment with execute permission.
Android NDK-built executables are linked for Android/Bionic, not Linux/glibc.
Android ARM64 and x86-64 undergo ELF architecture and dynamic-loader checks in
CI; **on-device / emulator execution is not yet included**.

FreeBSD and OpenBSD compile **inside their respective OS virtual machines** via
vmactions, not using Linux cross-compilers or relabeled ELF binaries. Their
smoke tests run on the BSD operating system. ARM64 BSD VM builds may be slower
under emulation and require successful CI evidence before publishing.

## Contents of a portable SDK archive

The archive expands to a `stage/` directory:

- `stage/bin/noqeri` or `stage/bin/noqeri.exe`: Stage-0 compiler
- `stage/bin/noqeri-nqri` or `stage/bin/noqeri-nqri.exe`: NQRI VM
- `stage/lib/`: portable graphics fastpath static library
- `stage/include/noqeri/`: native interface headers
- `stage/share/noqeri/`: Noqeri standard library, runtime modules, platform
  sources and branding

The portable CMake profile deliberately excludes the platform-specific
mailbox and PGAS native services. Graphics acceleration depends on actual
device/driver backends, which are **not** qualified by these SDK archives.

## Build locally

On Linux, Windows, macOS, FreeBSD or OpenBSD, install CMake 3.16+ and a C++17
toolchain, then run from the repository root:

```sh
cmake -S . -B build-portable -DNOQERI_GRAPHICS_PORTABLE_BUILD=ON -DNOQERI_ENABLE_SDL2=OFF -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-portable --config Release --target noqeri noqeri-nqri noqeri_graphics_fastpath --parallel 2
cmake --install build-portable --config Release --prefix stage
```

Test the extracted native SDK from the repository root:

```sh
./stage/bin/noqeri --version
./stage/bin/noqeri check examples/hello.noqeri
./stage/bin/noqeri-nqri --selftest
```

For Windows PowerShell, use `stage/bin/noqeri.exe` and
`stage/bin/noqeri-nqri.exe`.

### Android cross-build

Install the Android SDK, Android NDK **27.2.12479018** and CMake/Ninja on a
Linux machine, and set `ANDROID_HOME` to the SDK directory. For example:

```sh
cmake -S . -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_HOME/ndk/27.2.12479018/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release \
  -DNOQERI_GRAPHICS_PORTABLE_BUILD=ON -DNOQERI_ENABLE_SDL2=OFF -DBUILD_TESTING=OFF
cmake --build build-android --target noqeri noqeri-nqri noqeri_graphics_fastpath --parallel 2
cmake --install build-android --prefix stage
```

For Android x86-64 change `-DANDROID_ABI=x86_64`. A Linux build executable
must not be substituted for a genuine Android NDK output.

## Publishing release artifacts

1. Run **Noqeri cross-platform distributions** manually on the target commit
   and inspect every CI job; on ARM BSD guests allow for slow emulation.
2. After the matrix succeeds, create and push a release tag such as
   `v0.1.3` pointing to the same qualified commit.
3. The release job requires every OS/architecture job to pass and verifies
   that exactly one archive exists for each of the 12 targets.
4. The release contains each archive and a `SHA256SUMS` manifest. Check the
   checksum locally before extracting.

GitHub Actions archives are unsigned. Native `.deb`, `.rpm`, `.msi`,
`.pkg` and `.apk` installers, code signing/notarization, physical device
qualification and hardware GPU testing are **not** implemented by this workflow.
Do not claim them as release deliverables.

## Native executable target distinction

`noqeri build source.noqeri output.s` emits assembly for the compiler's
supported native backend contract. Distributing the host compiler and bytecode
VM to 12 host environments **does not** imply that Noqeri-generated code can
be linked to working native executable binaries on each of those environments.
`noqeri nqri` emits portable bytecode; OS-specific native executable
support requires separate ABI, assembler, linker and runtime acceptance tests.
