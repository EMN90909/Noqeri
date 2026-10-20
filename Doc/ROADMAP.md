# Roadmap

## 1.0 hardening

Increase parser/type-system conformance tests, complete formatter coverage, expand LSP capabilities, add fuzz targets and stabilize diagnostics.

## Native backends

Keep Linux x86-64 stable, then implement PE/COFF on Windows and Mach-O on macOS behind the same NIR contracts.

## Modules and standard library

Implement import semantics, module discovery, package resolution and a tested standard-library surface rather than adding fake APIs.

## Self-hosting

Grow noqeri until the compiler can be implemented in `.noqeri`, then maintain a reproducible staged bootstrap.

## Tooling

Publish the editor package once LSP behavior is stable, add debugger protocol support and automate release artifacts/checksums.
