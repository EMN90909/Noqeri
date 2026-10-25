# Noqeri distribution and target qualification

The workflow at `.github/workflows/distribution.yml` builds the portable Stage-0 compiler and creates archives on tagged releases. The archives contain a **host compiler**, not a promise of native executable generation for every target.

## Release process

1. Merge and verify the distribution workflow on each host.
2. Tag a tested commit (for example `v0.1.3`) and push the tag.
3. Wait for all six build jobs and the publish job to succeed.
4. Download the archive from GitHub Releases and verify it against `SHA256SUMS`.
5. Extract the archive and run `stage/bin/noqeri --version` (Windows: `stage/bin/noqeri.exe --version`).

## Release artifacts

| Host | Architecture | Archive |
| --- | --- | --- |
| Linux | x86-64 | tar.gz |
| Linux | AArch64 | tar.gz |
| Windows | x86-64 | zip |
| Windows | AArch64 | zip |
| macOS | x86-64 | tar.gz |
| macOS | AArch64 | tar.gz |

These are CI build candidates until the runners complete successfully. Builds are unsigned. The workflow does not yet produce OS-native installers.

## Additional targets

Android: implement and test a Termux/NDK build separately, then add an Android runner or emulator smoke test. A command-line compiler is not an APK.

FreeBSD/OpenBSD: use native BSD runners or reproducible VMs, compile with Clang/CMake, execute tests, then publish verified archives. Do not label Linux binaries as BSD compatible.

Custom operating systems: document the target ABI, executable format, syscalls, libc availability, linker and runtime requirements. Cross-compilation requires target-specific code generation and linking beyond packaging.

GPU architecture: shader output is currently a restricted WGSL subset. GPU driver support and device-specific native shaders require per-backend implementation and hardware qualification; the portable distribution workflow does not provide that.

## Native binary compilation

`noqeri build input.noqeri output.s` produces assembly for supported targets. It is not equivalent to producing a linked native executable. NQRI is a portable bytecode format and is distinct from platform-native binaries.

## Future release gates

- Native installer packaging: MSI/EXE, PKG, DEB, RPM.
- Android and BSD runner qualification.
- Package signing, notarization and supply-chain attestations.
- Cross-target native executable and GPU qualification.
- Verify that each archive contains runtime, standard library and other required files for full developer use.
