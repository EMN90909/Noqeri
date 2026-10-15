# NoeSQL — new database language and engine

The NoeSQL v0.1 design is the forward architecture for a Noqeri-owned typed database and relational query executor. Its `.noe` scripts and `.nqd` reference database snapshots are described in [Doc/NOESQL.md](../Doc/NOESQL.md). The current reference executor is implemented in Node.js and is not yet Noqeri-native or production-ready.

## Existing database components

The original Noqeri-owned `.nqr` modules in this directory retain their storage/transaction/WAL policies and helpers. New `noesql_lexer.nqr` and `noesql_ir.nqr` provide source-token and typed relational IR groundwork. The end-to-end NoeSQL host has not yet been implemented entirely in `.nqr`.

The legacy NoqeriDB command uses `.nqd` *scripts* with a different grammar and `.nqdb` data files; it remains for backward compatibility. **Do not confuse legacy `.nqd` scripts with new NoeSQL `.nqd` database snapshots.** Future migration must resolve that extension collision; users should invoke the new `noqeri noe` path explicitly.

Existing optional NoqeriSQL SQLite/PostgreSQL host-library adapters remain separate, so no direct NoeSQL-on-PostgreSQL/SQLite parity is implied. The database data formats are not interchangeable.

## Release conditions

Before a stable v1.0: self-hosted compiler validation, native parsing/semantics/execution, durable WAL, recovery and locking tests, foreign keys/unique-index enforcement, cross-platform fuzzing and crash injection, schema-aware migrations and verified backend adapters.

Use `node --test tests/noesql.test.mjs` for the portable reference tests without GitHub Actions.
