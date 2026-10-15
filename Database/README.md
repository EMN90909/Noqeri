# NoeSQL — new language and legacy NoqeriDB compatibility

NoeSQL is the intended Noqeri-owned database language and engine; see [NoeSQL architecture](../Doc/NOESQL.md) for its current experimental .noe/.nqd implementation. The old NoqeriSQL native SQLite/PostgreSQL adapters are kept as an interoperability bridge, while NoqeriDB is legacy compatibility. The former NoqeriSQL subsystem was the existing NoqeriDB host remains its legacy embedded compatibility path for Noqeri applications. The database core is owned by Noqeri source in this directory. The original Noqeri-owned policy modules use a narrow host capability boundary. The new optional NoqeriSQL adapters intentionally use the real SQLite3 and PostgreSQL/libpq host libraries for SQL execution.

See [NOQERISQL.md](NOQERISQL.md) for the proposed SQLite and PostgreSQL backend types and their current implementation status. Both backends have host library adapters through the new SQL CLI, but PostgreSQL server interoperability and the full build are not yet verified.

## Production model

The engine is split into independently testable policy modules:

- `index.nqr` — ordered integer indexes and unique lookup primitives.
- `transaction.nqr` — transaction state and isolation policy.
- `wal.nqr` / `recovery.nqr` — WAL integrity, checkpoint and recovery decisions.
- `query.nqr` / `planner.nqr` — prepared-query validation and basic plan selection.
- `migration.nqr` — monotonic schema-version rules.
- `security.nqr` — constant-work secret comparison, identifier validation, role checks, encryption-size rules and authentication backoff.
- `storage.nqr` — the narrow host capability boundary for files, locks, randomness, fsync and AEAD.
- `engine.nqr` — safe transaction/open/commit orchestration.

Persistent providers must use atomic replacement or WAL, fsync committed data before reporting success, reject corrupt pages, enforce exclusive writer locks, keep encryption keys outside database files, and use authenticated encryption. The reference contract requires 32-byte encryption keys, 12-byte nonces and 16-byte authentication tags.

## Security rules

Prepared statements are the default SQL interoperability path. Application values must never be concatenated into SQL. Storage providers must reject path traversal, cap page/WAL/database sizes, use cryptographically secure random nonces, and fail closed when authentication tags do not verify.

## Release gate

Do not describe NoqeriDB as production-ready until the current compiler can execute `tests/noqeridb_production.nqr`, crash/recovery fault injection passes on supported operating systems, corrupted WAL/page corpora are rejected, transaction atomicity tests pass, and benchmarks have published baselines. This directory implements the production architecture and safety policy; the release label is evidence-gated.
