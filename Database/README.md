# NoqeriDB

NoqeriDB is the embedded database path for Noqeri applications. The database core is owned by Noqeri source in this directory. Host code is restricted to operating-system capabilities such as files, locks, secure randomness, fsync, and authenticated encryption.

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
