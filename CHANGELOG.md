# Changelog

### 2026-10-26 — syntax simplicity and experimental Stage-2 counted repetition

- Implement typed single-expression functions `function twice(x: int): int = x * 2` and `for item in items` for typed arrays/slices in Stage-0.
- Add matching Stage-2 parser, AST, semantic and NIR source implementations for `repeat`, typed expression bodies and `for-in`. Source typecheck is green but runtime parity remains blocked by [issue #115](https://github.com/EMN90909/Noqeri/issues/115).
- Add bounded no-allocation numeric sum/checked sum, mean, count, filter and elementwise-map helpers, plus caller-owned `readText`/`writeText` file wrappers.
- Add executable Stage-0 examples and focused CI verification, including diagnostic and negative-syntax tests.
- Defer private inferred argument types, allocator-owned List literals, anonymous record literals, string interpolation, `?.`/`??`, typed `?` propagation, and full iterator/AI-framework binding syntax until semantic/backend parity is demonstrated.


### Post-tag v0.1.4: optional AI framework and registry package

- Add reverse-mode dense 2D tensor autodiff with a gradient-checked tape, softmax/cross-entropy, embedding row gathering and causal masks.
- Train a bounded single-layer causal attention + feed-forward neural language model, including deterministic byte-pair subword merges and `.nqtf` save/load.
- Add reference scalar, cache-tiled and SSE2 CPU tensor backends, exact backend selection and parity tests. No GPU acceleration claimed.
- Publish the Noqeri source package `noqeri/ai-framework@0.1.4` through the separate source-index-backed registry repository.
- No compiler grammar changes; the native C++ SDK is not automatically linked by a Noqeri import. Experimental, not production-qualified.


### Post-tag v0.1.4: lightweight neural language-model research add-on

- Add a bounded, dependency-free C++17 CPU numerical SDK (matrix multiplication,
  stable row softmax, RMS normalization and forward-only causal attention).
- Add a miniature trainable character-level neural next-token model with safe,
  versioned offline model persistence, reproducible seeded generation and
  `noqeri ai lm train/generate` commands.
- Extend deterministic and rejection-path tests and package as unique, SHA-qualified
  v0.1.4 add-on assets; the original v0.1.4 tag and old binaries are unchanged.
- Scope: **not** full LLM pretraining, transformer autodiff or GPU acceleration.

## 0.1.4 — 2026-10-26 (experimental developer preview)

- Align CMake, compiler/language version constants and project manifests with 0.1.4.
- Add standalone native Noqeri web executable bundling (C++17 bootstrap), layout slots, parameterized routes, server-side byte-length validation and end-to-end tests.
- Add bounded concurrent HTTP ingestion, optional Basic access control, secure-cookie switch, structured status logging, and typed SQLite query scalars; still localhost-only and experimental.
- Add WebSocket frame qualification, optional SQL driver inspection, NoeSQL-first backend integration carried forward from 0.1.3.
- Add scoped GPL-3.0-only toolchain / Apache-2.0 first-party library distribution policy, dual license texts and qualified prerelease workflow.
- **Not yet complete:** Stage-2 typed NIR web lowering, production TLS/authz/identity lifecycle, real native NoeSQL session ABI provider, multi-backend transaction testing, WebAssembly DOM bridge and cross-platform production certification.
- See [v0.1.4 qualification and release limitations](Doc/V0_1_4_RELEASE.md).


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
