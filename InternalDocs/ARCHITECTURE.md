# Noqeri architecture

Noqeri uses a **Noqeri-first implementation rule**: reusable language-facing behavior is written in `.noqeri` whenever the current language can express it. C++ is the bootstrap implementation of the compiler and the narrow host adapters needed for filesystem/process/ABI access that Noqeri does not yet expose safely.

The compilation path is:

`Noqeri source -> lexer/parser -> checked AST -> generics/lifetime analysis -> typed NIR -> optimizer -> interpreter/native/web backend`

`Include/noqeri/noqeri.hpp` is the public bootstrap API contract. The compiler still retains an internal compatibility namespace/include name from the earliest bootstrap; it is not part of the `.noqeri` language surface.

## Noqeri-owned implementation

These directories now contain executable Noqeri source rather than README placeholders:

- `Modules/` — built-in/native module semantics.
- `Objects/` — value tags and byte/text behavior.
- `Database/` — NoqeriDB lookup/insert/update/delete/read algorithms.
- `Lib/std/` — integer, algorithms, slices, sort/search, statistics, bytes, UTF-8 validation, checksums, memory, status, time, deterministic random, matrix/range/window helpers, text, set/map/stack/queue, bit and validation helpers.
- `Lib/security/` — secure zeroing, full-scan equality and safe ranges.
- `Lib/test/` — test assertions.
- `Parser/` — source-level ASCII/token/scanning rules used as progressive self-hosting substrate.
- `Runtime/` — source-level runtime status, memory and limits helpers.
- `Programs/` — executable self-test plus diagnostics/version policy.
- `Platforms/`, `PC/`, `Mac/`, `Android/`, `PCbuild/` — executable target/capability/build-profile policy.
- `Tools/security/`, `Tools/fuzz/`, `Tools/build/` — security policy, deterministic fuzz generation and build policy.

`tests/noqeri_owned_components.noqeri` tests the foundational Noqeri-owned layers. `tests/noqeri_library_suite.noqeri` imports and exercises the expanded standard-library, parser/runtime/platform/tooling surface through the real compiler and interpreter. CTest runs both explicitly.

## Compiler/bootstrap boundary

Host/compiler C++ is physically concentrated under `Compiler/`:

- `Compiler/core.cpp`, `modules.cpp`, `type_checker.cpp`, NIR/optimizer/backends/linker and related compiler implementation.
- `Compiler/bootstrap/lexer_host.cpp` and `parser_host.cpp` bootstrap the current frontend while equivalent source-level parser capability grows under `Parser/*.noqeri`.
- `Compiler/bootstrap/abi_runtime_host.cpp` and `interpreter_host.cpp` provide the reference-host execution edge while source-level runtime behavior grows under `Runtime/*.noqeri`.
- `Compiler/bootstrap/cli_host.cpp`, `lsp_host.cpp`, `formatter_host.cpp`, `package_host.cpp` and `test_runner_host.cpp` are compiler/toolchain host adapters.
- `Compiler/bootstrap/noqeridb_host.cpp` retains `.nqd` parsing and `.nqdb` filesystem persistence until typed filesystem APIs can move that edge into Noqeri.
- `Compiler/bootstrap/security_audit_host.cpp` performs repository/package-lock filesystem traversal; reusable security policy stays in Noqeri.
- `Compiler/bootstrap/production_doctor_host.cpp` performs repository filesystem inspection; build policy itself also exists as Noqeri source.

`Parser/`, `Runtime/`, `Programs/`, `Database/`, `Modules/`, `Objects/`, `Lib/`, `Tools/security/`, `Tools/fuzz/`, `Tools/build/`, and the platform implementation folders are now enforced as Noqeri-owned areas: repository verification fails if C/C++ source is reintroduced there.

This does **not** mean Noqeri is already self-hosted. The frontend/backends and host-environment edges still compile from C++. Moving them blindly into `.noqeri` before the language has equivalent filesystem, process, dynamic data and compiler-construction facilities would make the toolchain less functional. The migration is therefore capability-by-capability rather than percentage-by-percentage.

## Ownership rule

When adding functionality:

1. Put reusable algorithm, policy, runtime, data-structure and library behavior in `.noqeri`.
2. Expose only the smallest environment capability through the bootstrap/ABI when Noqeri cannot reach it itself.
3. Keep remaining C/C++ under `Compiler/` or public embedding headers; do not grow C++ application libraries.
4. Add executable `.noqeri` integration coverage.
5. Promote a bootstrap function into Noqeri when the required language primitives exist; then remove the corresponding C++ implementation rather than keeping two owners.

The Unix/Windows repository verifiers and `noqeri doctor` reject the removed implementation-placeholder READMEs, reject C/C++ in Noqeri-owned areas, and enforce a minimum Noqeri source/stdlib floor. This is progressive self-hosting with a visible, shrinking bootstrap boundary.
