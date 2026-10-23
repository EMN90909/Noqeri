# Changelog

## 0.1.43-development — 2026-10-23 (candidate, not a stable release)

- Updated active public compiler, language version functions, project manifest,
  release-qualification report, and CI version expectations to 0.1.43.
- PGAS remains experimental until the hardware, self-hosted compiler, and
  consensus qualification gates are met.
- The checksum-pinned trusted Stage-1 seed continues to expose its historical
  0.7.0 self-hosted source version. No Git history or archived registry release
  has been rewritten.


## 0.1.3-development — 2026-10-23 (version alignment, not a stable release)

- Reset the active C++ compiler, language API, project metadata, and release-report version declarations to 0.1.3 at the user's request. The trusted Stage-1 compiler source and bytecode remain pinned to the historical 0.7.0 bootstrap artifact pending an independently reproducible trust rotation.
- Historical changelog entries, Git commits, and published immutable registry packages retain their original versions. This is a deliberate SemVer downgrade from the previous 0.7.0 development line, **not** an assertion of feature removal or backward compatibility.
- PGAS 0.1 preview was merged; secure multi-host, RDMA, compiler attributes, remote job execution, and crash recovery remain separately qualified.


## 0.7.0-development — 2026-10-18 (not yet released)

- Added portable NQON (`.nqon`) with a standalone parser/serializer, validation, comments, JSON conversion and canonical `project.nqon` manifests; legacy `project.noqeri` remains readable during migration.
- Simplified `noqeri new`: generated projects now expose only `name` and `version` in `project.nqon`, while the compiler supplies conventional defaults, and the starter source begins directly with `print(...)`.
- Added an explicit beginner-syntax simplicity contract and a 9.7/10 internal design target. The number is not an empirical usability claim; comparative claims still require the repository usability-study evidence.

- Normalized active compiler and project version declarations to a 0.7.0 development track. This is a deliberate pre-1.0 version reset; the older 1.x changelog entries remain historical, not evidence of a production release.
- Added an initial game SDK for 3D vector math, bounded fixed-step simulation, action-button state and axis-aligned collision/ray queries, with a conformance fixture.
- Added evidence-driven game release criteria. Native executable/graphics support and end-to-end compiler qualification are **not verified**.


## 1.4.0-general-systems — 2026-09-11

- Added fixed arrays (`[T; N]`), array literals, slices (`[]T`), `slice(...)`, indexed access, and `len(...)`.
- Added organizational `module` declarations and recursive relative file imports through `import "file.noqeri"`.
- Added inferred register-value function generics such as `function identity<T>(value: T): T`.
- Added allocation-free integer status propagation through `throw` and `try`.
- Added typed sequentially-consistent atomic load, store, exchange, compare-exchange, and fence operations.
- Added explicit CPU intrinsics (`x86.pause`, `x86.rdtsc`, `x86.halt`, `compiler.fence`) and constrained one-instruction inline assembly.
- Extended NIR with stack allocation, slice, atomic, intrinsic, inline-assembly, and error-propagation operations.
- Lowered the new low-level operations directly in the freestanding x86-64 backend without introducing an OS or ABI runtime dependency.
- Added import-graph compilation to CLI and test tooling plus cross-platform regression coverage for the new features.
- Updated the language reference, grammar, production doctor, CMake tests, examples, and public site for language 1.4 / ABI v2.

## 1.3.0-systems — 2026-09-11

- Added portable-width integer types (`u8` through `u64`, `i8` through `i64`, `usize`, `isize`).
- Added pointers, address-of/dereference, pointer indexing, explicit casts, volatile access, deterministic record layout, and extern/export functions.
- Added explicit memory NIR operations and width-aware freestanding x86-64 memory lowering.
- Kept systems types general-purpose rather than introducing a kernel or OS-specific language mode.

## 1.2.0-freestanding — 2026-09-11

- Removed Linux syscall, `_start`, GNU `ld -m elf_x86_64`, and ELF-specific assembly assumptions from the native compiler path.
- Introduced the freestanding `noqeri_entry(noqeri_abi*)` entry contract and separated target assembly from platform object/link adapters.
- Made Noqeri ABI v2 the project-owned environment boundary.

## 1.0.0-production-track — 2026-09-11

- Established **noqeri** as the canonical language name.
- Changed public source files to **`.noqeri`**.
- Renamed the CLI to `noqeri`, manifest to `project.noqeri`, and lock file to `noqeri.lock`.
- Reorganized the repository around mature language-runtime responsibilities inspired by CPython.
- Moved the buildable bootstrap compiler into `Compiler/bootstrap/`.
- Added a canonical wolf/code brand asset and reused it in the README, site and editor file icons.
- Added VS Code, JetBrains/TextMate, Vim/Neovim and Sublime integration.
- Added public docs, internal architecture docs, platform ownership, release policy and stronger repository doctor checks.
