# Noqeri database components (integration status)

**Canonical database engine: NoeSQL.** This directory contains historic NoqeriDB policies and overlapping NoeSQL frontend modules. The NoqeriDB host codec (`NQDB1`) is legacy only and **must not** be presented as the engine for new `.noedb` databases.

- `Database/noesql*.noqeri`: compiler/frontend and policy modules originally mirrored from NoeSQL; until imported from the NoeSQL-owned package they are compatibility scaffolding and must not diverge. NoeSQL owns the parser, planner, executor, isolation, WAL and Zig pager.
- `Database/index.noqeri`, `transaction.noqeri`, `wal.noqeri`, `recovery.noqeri`, `query.noqeri`, `planner.noqeri`, `migration.noqeri`, `security.noqeri`, `storage.noqeri`, `engine.noqeri`: legacy NoqeriDB policy experiments. Valuable behaviors should migrate into corresponding NoeSQL layers once native E2E tests pass, rather than being deleted on filename alone.
- `Database/noqeridb.noqeri` and `Compiler/bootstrap/noqeridb_host.cpp`: deprecated compatibility path. `noqeri db <fixture.nqd> <legacy.nqdb>` continues to support historic fixtures with a deprecation diagnostic, but explicitly rejects using `.noedb` with the incompatible legacy codec.
- `Lib/std/noesql.noqeri` is the staged beginner-facing API with `database(path)`, prepared statements and transaction wrappers; its native `noesqlHost...` symbols still need a verified NoeSQL session-v2 provider.
- `Compiler/bootstrap/sql_host.cpp` contains **external** SQLite/PostgreSQL adapters and retains defensive SQL and TLS checks; neither engine replaces NoeSQL.

For new databases, use NoeSQL's canonical `.noedb` pager/WAL codec. Do not rename a legacy `.nqd/.nqdb` file. Run the explicit NoeSQL migration tool instead. This integration branch does not yet contain a qualified native Noqeri → NoeSQL compiled program, nor does it remove the duplicate legacy implementations. See `Doc/ECOSYSTEM_INTEGRATION.md`.

## Release gate

The cleanup completes only after imports point to the sole NoeSQL engine, native ABI/provider link is verified, legacy capabilities are moved or retained as separate migration fixtures, and `.noqeri` → `.nqri` → `NoeSQL` → `.noedb` passes real CRUD/rollback/index/WAL/concurrency/corruption regression tests.
