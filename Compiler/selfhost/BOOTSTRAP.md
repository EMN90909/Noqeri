# Noqeri bootstrap ownership

`bootstrap.nqr` is the source entry point for a normal stage-1 build. Compiler policy, lexical rules, syntax checks, type-width policy, package-coordinate validation, and package archive limits are Noqeri-owned.

A trusted stage-0 executable is still necessary for the first translation of `bootstrap.nqr` into `noqeri-stage1.nqo`. That is a bootstrap trust boundary, not a C/C++ production dependency. The historical C++ seed remains on `bootstrap/cpp17-stage0` for reproducibility.

The portable `.nqo` object currently uses Node.js only as a host for filesystem/process/network capabilities. Those host capabilities must not own language semantics. Removing that host is a later native-backend milestone and should not be claimed complete until stage1 builds stage2 and equivalence tests pass.

## Self-hosting proof gate

The verifier `scripts/bootstrap-stage2.mjs` now requires an executable `commands.stage0_builds_stage1` step and explicit `stage0_built_stage1: true` provenance before it will invoke Stage 1. It then requires Stage 1 to build Stage 2, Stage 2 to rebuild the compiler, equivalent build outputs, the full suite, and a release not dependent on C++. The example config intentionally fails closed. A successful build without an executable bootstrap chain is not evidence of self-hosting.

Cross-language interoperability uses the versioned embedding C ABI (`Doc/ABI.md`, `Doc/FFI.md`), experimental native FFI bindings, and restricted browser host calls. Compatibility with every programming language is not achievable as a single compiler feature: each foreign-language adapter must define ABI representations, ownership, error mapping and conformance tests. Native exported symbols are **not** yet general stable C ABI exports.
