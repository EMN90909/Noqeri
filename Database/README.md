# Noqeri database boundary

**NoeSQL** (repository `EMN90909/NoeSQL`) is the only canonical Noqeri-native database engine. Its native storage is `*.noedb` with the NoeSQL-defined Zig/page and WAL format. Noqeri compiler/runtime must bind the versioned NoeSQL ABI and must not separately fork the query planner, executor, indexes or WAL.

## Legacy compatibility, not a competing engine

`Compiler/bootstrap/noqeridb_host.cpp`, `Database/noqeridb.noqeri`, `Database/engine.noqeri` and related policy files represent the **legacy NoqeriDB** implementation, with `.nqd` scripts and `.nqdb` database snapshots. They are retained as migration fixtures and explicit legacy compatibility. Their `NQDB1` snapshot is **not** the `NQDB0002` native NoeSQL format. Never rename a snapshot to `.noedb` or reinterpret its bytes.

`Database/noesql_*.noqeri` currently hold compiler conformance modules and prototype policy, not an independently deployed production engine. Canonical feature implementation and release gating belong upstream in NoeSQL. Stop adding new storage or SQL features here.

## Application boundary

`Lib/std/noesql.noqeri` exposes `database(path)` and prepared query, execution, transaction and error helpers above a narrow host/provider API. The provider must be linked to the versioned NoeSQL C ABI. Its Noqeri host capability implementations are **not yet linked or qualified**. Code using those services is not runnable until that integration passes. The database facade does not imply that the illustrative dotted collection API is supported by the grammar.

SQLite and PostgreSQL remain *external* adapters, not alternatives to NoeSQL ownership. Keep `Compiler/bootstrap/sql_host.cpp` defensive checks, parameter binding and PostgreSQL TLS verification unchanged.

## Migration and gates

For supported legacy JSON NoeSQL snapshots, use the explicit upstream NoeSQL `migrateLegacyNoedb(source,destination)` path. Legacy `NQDB1` NoqeriDB snapshots require a separate validated converter and are **not accepted** by that path. Preserve recovery/corruption test fixtures rather than mass-renaming them.

Native `.noqeri → .nqri → Noqeri VM → NoeSQL ABI → .noedb` execution still needs a real linked provider and trusted compiler bootstrap evidence. Do not claim a successful end-to-end flow without those tests.
