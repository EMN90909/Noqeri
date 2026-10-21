# Noqeri ecosystem, canonical boundary (integration qualification)

One application language is Noqeri. NoeSQL is the engine that owns database semantics and `.noedb` persistence. The Registry owns package/version/advisory metadata in `registry.noedb` and stores immutable `.nqpkg` bytes outside the database. The CLI and website are Registry HTTP clients.

| Extension | Role | Status |
| --- | --- | --- |
| `.noqeri` | language source | canonical |
| `.nqri` | compiler/runtime artifact | canonical, native bootstrap requires qualification |
| `.nqon` | object notation/config | reserved; **parsing not qualified** |
| `.noedb` | NoeSQL database | canonical, Zig NQDB0002 format |
| `.nqpkg` | immutable package tar+gzip archive | Registry v1 |
| `.nqr`, `.nqo` | legacy source/object | compatibility/test fixtures only |
| `.nqd`, `.nqdb` | legacy database formats | explicit conversion only |

## Dependency directions

- Noqeri `Lib/std/noesql.noqeri` provides an application-facing session wrapper for the external NoeSQL C ABI. The native linkage still requires a qualified NoeSQL provider.
- The authoritative ABI is `NoeSQL/Native/cpp/noesql_runtime_abi.h` (v1, opaque database + prepared statement handles). Noqeri must link to that ABI, not clone an executor.
- `Database/noqeridb.noqeri`, `Compiler/bootstrap/noqeridb_host.cpp` and related files remain deprecated NQDB1 compatibility modules. They are **not** the canonical database backend. The `noqeri db` command emits a deprecation warning. Do not use it to create new projects.
- `Database/noesql*.noqeri` contains a historical NoeSQL compiler/frontend integration mirror. Source ownership must converge on the independent NoeSQL repository without deleting unported security or compiler logic.
- `Compiler/bootstrap/sql_host.cpp` remains an external SQLite/PostgreSQL adapter and must retain parameter binding, transactions, SQLite hardening and verified PostgreSQL TLS.
- `Runtime/package-manager.mjs` resolves Registry `/v1` metadata, verifies complete SHA-256 archive bytes, extracts only bounded, regular, relative files, and checks cached package file hashes. GitHub source-directory access is opt-in legacy test compatibility.
- Pinned yanked releases remain downloadable to preserve lockfile reproducibility; unpinned resolution excludes them. Publish and update flows must never change bytes for an existing version.
- Set `NOQERI_REGISTRY_URL` to a trusted HTTPS origin; `http://localhost` is allowed only for development.

## Database migration, never relabel bytes

NoeSQL `noesql migrate old.nqdb app.noedb` accepts only NoeSQL's legacy checksummed reference snapshot format. It must reject the separate NoqeriDB `NQDB1` format and malformed files, not silently reinterpret them. NQDB1 export/import conversion into NoeSQL has **not** been implemented. Original files must be retained until an independently verified migration exists.

## Executable release gate

Do not claim success for `.noqeri → .nqri → native runtime → NoeSQL → .noedb` until a trusted compiler seed, actual linked provider, full transaction/WAL/crash/concurrency tests and repeatable end-to-end evidence exist. Neither a C interface table nor Node reference execution proves native bootstrap. The Registry currently uses a transitional Node transport/catalog initializer pending native Noqeri service qualification.
