# Changelog

## Unreleased — NoeSQL 1.0 design implementation candidate (2026-10-15)

- Preserved the active self-hosted compiler architecture; archived C++ Stage-0 recovery provenance is deliberately retained until reproducible compiler equivalence is established.
- Added `Database/noesql_lexer.nqr`, `noesql_parser.nqr`, `noesql_ir.nqr`, and `noesql.nqr` as native frontend/IR groundwork. Not yet compiled through the self-hosted toolchain.
- Added a runnable transitional `Runtime/noesql/` lexer, recursive-descent + precedence parser, schema catalog, typed query IR validation, row executor, and checksummed `.nqd` snapshot persistence.
- Added `.noe` syntax validation and execution to the Stage-2 CLI via `noqeri noe`; retained native SQLite/PostgreSQL SQL adapter commands separately.
- Added 16 passing local Node.js reference tests, an offline shell runner, a NoeSQL fixture and an unverified native Noqeri IR test source.
- **Not a stable release:** no full repository build, native `.nqr` NoeSQL executor, indexed query implementation, WAL/MVCC, PostgreSQL live server tests, crash recovery or cross-platform verification.


## Unreleased — NoqeriSQL v1.0.0 candidate (2026-10-15)

- Added host-library SQLite and PostgreSQL SQL execution through a separately built native adapter and optional bootstrap CLI command.
- Added optional native Stage-2 delegation controlled by NOQERI_SQL_ADAPTER.
- Made SQLite scripts transactional with WAL, foreign key enforcement, defensive execution, and prohibited transaction escapes.
- Added bounded script loading, parameter checks, stricter legacy database decoding and schema comparison, writer locking, atomic replacement and fsync protections.
- Added dialect policy, SQL language tests and SQLite concurrency/rollback integration tests.
- Release gates remain open: full self-hosted build, PostgreSQL server integration, cross-platform and crash-fault tests. No v1.0.0 release tag has been created.


## 1.4.0-general-systems — 2026-09-11

- Added fixed arrays (`[T; N]`), array literals, slices (`[]T`), `slice(...)`, indexed access, and `len(...)`.
- Added organizational `module` declarations and recursive relative file imports through `import "file.nqr"`.
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
- Changed public source files to **`.nqr`**.
- Renamed the CLI to `noqeri`, manifest to `project.nqr`, and lock file to `noqeri.lock`.
- Reorganized the repository around mature language-runtime responsibilities inspired by CPython.
- Moved the buildable bootstrap compiler into `Compiler/bootstrap/`.
- Added a canonical wolf/code brand asset and reused it in the README, site and editor file icons.
- Added VS Code, JetBrains/TextMate, Vim/Neovim and Sublime integration.
- Added public docs, internal architecture docs, platform ownership, release policy and stronger repository doctor checks.
