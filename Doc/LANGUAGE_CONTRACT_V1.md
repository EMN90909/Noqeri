# Noqeri 1.0 language contract (stabilization candidate)

Version **1.0.0**, edition **2026**, language version **1.0**; the public C++ embedding ABI is `noqeri-abi-v2` and Registry HTTP protocol is `v1`. Versioned metadata is a release identity, not proof of a production-qualified compiler.

## Canonical source and package formats

| Use | Canonical form | Compatibility policy |
| --- | --- | --- |
| Source | `.noqeri` | Old `.nqr` source is not accepted by the canonical compiler; historical Registry releases may retain it |
| Portable compiled module | `.nqri` | Do not rename old `.nqo` binaries; rebuild them |
| Project metadata | `project.nqon` | Legacy `project.noqeri` is a migration-only compatibility input |
| Package archive | `.nqpkg` | SHA-256 over archive bytes; strict path, size and integrity checks |
| Database file | `.noedb` | Legacy `.nqd/.nqdb` require explicit NoeSQL migration; filenames alone are not a format conversion |
| Lockfile | `noqeri.lock` | Lock by exact version and verified artifact digest; reject yanked versions on online resolution |

Noqeri syntax, type checking, generics, imports, integer behavior, pointer safety and target ABI layouts must match the executable language conformance suites. No compiler optimization may alter trapping, allocation visibility, integer overflow, memory or concurrency semantics.

## API and compatibility rules

- `1.0.x` patch releases must not intentionally break programs that were accepted and correctly executed by 1.0.0; behavior changes require regression tests and review.
- Additive APIs require coverage and explicit capabilities; incompatible syntax, ABI, serialized NQRI or protocol changes need a new major version or explicit negotiated format version.
- Raw pointer dereference and member access require explicit `unsafe`. Database wrappers do not silently weaken pointer validation.
- NoeSQL is the canonical Noqeri database implementation. SQLite and PostgreSQL are external adapters, not interchangeable `.noedb` on-disk engines.
- Compiler flags `-O0 -O1 -O2 -O3 -Os -Oz` must preserve observable behavior. Only transformations with verified preconditions may run.
- C++ Stage-0 is the audited bootstrap dependency; self-hosting and its independent seed trust are separate proof requirements. Do not call a locally reproducible seed independently trusted.

## Stability decision

**Candidate, not production-qualified.** A stable announcement requires passing `scripts/qualify-release.mjs`, verified Stage-1 → Stage-2 → Stage-3 bootstrap, native ABI/conformance on the declared supported platforms, and an independently verifiable artifact manifest and signature. See `Doc/RELEASE_GATE_V1.md` and `release/platforms-v1.json`.

Any missing qualification evidence is a blocker rather than a reason to weaken the checks.
