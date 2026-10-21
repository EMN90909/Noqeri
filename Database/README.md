# Database integration for Noqeri

**Canonical direction: NoeSQL**, with `.noedb` storage and the `noesql` standard-library interface (`Lib/std/noesql.noqeri`). NoqeriDB and NoqeriSQL are **legacy compatibility implementations**, not additional database products to choose for new projects.

## What works today

- `Lib/std/noesql.noqeri` defines the Noqeri-side capability facade and explicit unsafe handle boundaries. A fully verified Stage-2-to-native NoeSQL driver is **not yet connected**.
- The separate private **NoeSQL** repository owns its reference `.noedb` pager, planned native engine, parser, WAL and session ABI.
- SQLite and PostgreSQL remain external SQL adapters through their own installed drivers. They are not NoeSQL file formats.
- `Compiler/bootstrap/noqeridb_host.cpp` still supports legacy `.nqd/.nqdb` behavior for existing code and migration. It is **not** the canonical engine. Deleting it today would break existing compiler/CLI compatibility before migration parity is proven.
- This directory's `noqeridb.noqeri`, `engine.noqeri`, `transaction.noqeri`, `wal.noqeri`, `planner.noqeri`, etc., are historical experimental NoqeriDB modules. Do not treat them as production-qualified database implementation or new canonical API.

## Migration policy

1. Back up original `.nqd/.nqdb` data, verifying its format and checksums.
2. Use NoeSQL's explicit one-way `migrateLegacyNoedb()` path to create a **new** `.noedb` file. Never rename a database file as a substitute for conversion.
3. Verify the new file with the NoeSQL reference verifier and a fresh reopen/query round trip.
4. Keep original files until application behavior and recovery are tested. Do not automatically redirect legacy app code to a different engine.
5. Remove old NoqeriDB code only after every native caller has migrated and the compiler, release and data-recovery suites pass.

Read the separate NoeSQL repo's `README.md` and `Doc/CI_RUNNER_DIAGNOSIS.md`. Private NoeSQL Actions runners currently lack completed native qualification evidence; reference-store and public Registry contract tests do **not** establish native compiler/database production safety.

For SQLite/PostgreSQL compatibility command details see [NOQERISQL.md](NOQERISQL.md), which documents the legacy adapters, not a third canonical engine.
