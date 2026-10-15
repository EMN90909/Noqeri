# NoqeriSQL — compatibility architecture (experimental)

NoqeriSQL is the proposed successor name for the NoqeriDB subsystem. This change introduces an explicit dialect policy **without claiming** PostgreSQL or SQLite implementation compatibility.

## Backends

| Type | Model | Current state |
| --- | --- | --- |
| Embedded | Noqeri-owned .nqdb file, legacy .nqd scripts | Existing host implementation; not production ready |
| SQLite | SQLite database file via a future adapter | Design contract only; no SQLite file reader or SQL executor |
| PostgreSQL | Client/server via a future PostgreSQL protocol adapter | Design contract only; no wire client, authentication or SQL executor |

The existing `noqeri db` CLI and `NoqeriDatabase` C++ API are **retained** for backwards compatibility. Legacy files are not renamed or migrated automatically. `Database/dialect.nqr` defines backend selection policy, not a connected driver.

## Required for actual SQL support

1. Implement a standard SQL tokenizer, grammar and typed AST; reject unsupported statements instead of silently interpreting custom .nqd commands.
2. Support parameter binding through backend-native prepared statements. Never interpolate bound values. Identifiers require separate allowlisted quoting.
3. SQLite adapter: use a verified SQLite library, enforce safe PRAGMAs and busy/transaction behavior, preserve SQLite's file format, and test foreign keys, locking and migration compatibility.
4. PostgreSQL adapter: use a maintained protocol implementation, require TLS verification outside explicit development configurations, support SCRAM authentication, transaction statuses and cancellation, and test protocol errors.
5. Expose typed error/status mapping, feature negotiation, resource limits, transaction/isolation levels, and real integration tests for each backend.
6. Establish migration rules from .nqdb with backups, schema validation and reversible export. Never pretend .nqdb is .sqlite.
7. Run crash-injection, concurrent writers, truncated/corrupt file and recovery tests on Windows, Linux and macOS before production status.

## Safety warnings

The legacy C++ host loads its complete file into memory and writes whole-file snapshots. It does not yet implement multi-process locking, WAL recovery, cryptographic integrity, complete file size limits or a cross-platform atomic replacement protocol. This update removes the destructive pre-rename delete; an existing database may cause a safe commit failure on Windows until a durable replacement mechanism is implemented.

Do not use this engine to store sensitive or critical production data. Keep tested backups.
