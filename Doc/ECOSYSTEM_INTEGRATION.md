# Noqeri ecosystem contract (migration branch)

This document describes the implementation *and* remaining qualification gates, not a claim that the whole system is production-ready.

## Extensions

| Extension | Role |
|---|---|
| `.noqeri` | Canonical Noqeri source |
| `.nqri` | Portable compiled intermediate/runtime module |
| `.nqon` | Noqeri Object Notation and project metadata |
| `.noedb` | Canonical NoeSQL database format |
| `.nqpkg` | Content-addressed deterministic package archive |

`.nqr`, `.nqo`, `.nqd`, and `.nqdb` are **legacy**. Historic release payloads and compiler migration tests should stay available until republished and qualified. Rename is NOT a safe database format conversion: legacy `.nqd/.nqdb` need the NoeSQL migration CLI.

## Language/database ownership

Noqeri is the language/compiler/standard library; NoeSQL is the native database engine. `Lib/std/noesql.noqeri` defines beginner-facing `database(path)`, `database_execute`, `database_query`, transactional operations and error reporting. Its `noesqlHost...` functions must link to the NoeSQL v2 session ABI, not a local NoqeriDB implementation.

**Important qualification limit:** The compiler/bootstrap currently does NOT provide this host implementation. The Noqeri source API is staged and cannot claim runnable `hello.noqeri → hello.nqri → NoeSQL` end-to-end until a real Noqeri runtime driver is linked and tested. The legacy `Compiler/bootstrap/noqeridb_host.cpp` emits a separate NQDB1 format and remains **deprecated / compatibility only**, not a second canonical engine. `Runtime/noesql-reference.mjs` is a reference test oracle and must not be treated as native production performance.

SQLite and PostgreSQL remain optional external adapters. The security checks in `Compiler/bootstrap/sql_host.cpp` should not be weakened.

## Registry client

Stage-1 CLI uses `NOQERI_REGISTRY_API_URL` (HTTPS; localhost HTTP development) for `GET /v1/index`, release metadata and deterministic `.nqpkg` bytes. Both unpinned and explicitly pinned yanked versions are refused from online downloads. A verified cached pinned package can still work offline.

The CLI checks the exact SHA-256 of the downloaded archive **before** bounded extraction and checks cached file integrity again before reuse. Package manifests can temporarily be `package.nqr` for legacy releases, but new packages should publish `package.noqeri`. `project.nqon` and `noqeri.lock` persist resolved dependencies. Advisory fetch also defaults to Registry `/v1/advisories`, not GitHub raw files.

The official Registry service stores only metadata in `registry.noedb` and separate content-addressed archives on filesystem/object storage. Noqeri Web uses HTTP and never reads the Registry database directly.

## Release gates

1. Qualify NoeSQL v2 session ABI native implementation with Zig-backed storage and Noqeri Stage-2 compiler.
2. Migrate NoqeriDB useful functionality and remove its canonical CLI exposure while retaining explicit legacy conversion tests.
3. Confirm compiler NQRI loading/running of `.noqeri` CRUD application and NoeSQL WAL/crash/concurrency tests.
4. Migrate existing Registry packages to canonical source/manifest extensions with backcompat and reproducible archive verification.
5. Set up authorized read-only access from public Registry CI to private NoeSQL. Run integration and security gates.
6. Confirm online Registry API + CLI installer + Web return matching package metadata on deployed service.

Public package host and Web mock-HTTP tests are passing in their integration branches, but they do not prove these release gates.
