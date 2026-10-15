# NoqeriSQL v1.0.0 candidate — SQL adapters

NoqeriSQL adds real SQL command execution using the installed SQLite3 and PostgreSQL/libpq libraries. It does not reimplement the PostgreSQL server or SQLite file format.

## Commands

    noqeri sql sqlite ./data.sqlite ./schema.sql
    noqeri sql postgres "host=db.example.org dbname=myapp user=myapp" ./query.sql

The legacy command remains supported for existing files:

    noqeri db ./legacy.nqd ./legacy.nqdb

Legacy .nqd syntax is NOT SQL and .nqdb is NOT an SQLite database.

## Build

CMake detects optional SQLite3 and libpq dependencies. Add -DNOQERI_REQUIRE_SQL_BACKENDS=ON when both drivers are mandatory for the build. Builds without an adapter reject requests for it.

## SQLite

- Uses the real SQLite SQL parser, engine and on-disk format.
- One SQL file is wrapped in BEGIN IMMEDIATE/COMMIT with rollback on statement failure.
- SQLite VFS coordinates cross-process writer locks; busy timeout is five seconds.
- Configures journal_mode=WAL, synchronous=FULL, foreign_keys=ON and trusted_schema=OFF.
- Enables defensive mode; denies transaction-control statements, savepoints, PRAGMAs, ATTACH and DETACH inside a managed script.
- Rejects NUL bytes, missing/empty/oversized scripts (1 MiB maximum), and unbound parameter markers.
- Limits SQL size, value size and column count. Query output is tab-separated and buffered until commit.
- Application values must use a parameter-binding API; the file-based CLI intentionally does not substitute variables.

## PostgreSQL

- Uses libpq with verified TLS (sslmode=verify-full forced) and 10-second connection timeout.
- Uses PQexecParams extended-query protocol; each script may contain ONE SQL statement only.
- Begins and finalizes a database transaction, printing results after successful commit.
- Does not interpolate application parameters. End-to-end PostgreSQL server tests have NOT been run.

## Compatibility and limitations

SQL dialects differ. PostgreSQL and SQLite are separate engines rather than interchangeable implementation modes. Application-facing prepared binding, migrations, pooling, schema export and dialect normalization are future tasks.

The legacy whole-file snapshot engine has improved validation, bounds and an exclusive writer lock directory, but needs durable cross-platform replace, corruption checksums, fault injection and verified crash recovery. If a legacy writer crashes, inspect any stale .writer.lock directory before deleting it.

## Release gate and local evidence

Local Linux C++17 build of the standalone SQL host passed with SQLite 3.46.1 and libpq 17.11. Isolated testing passed standard SQLite SQL, rollback, 64 concurrent writer processes, malicious COMMIT rejection, foreign-key enforcement, malformed file rejection and script limits. An isolated CMake build passed 2/2 CTest tests. ASan/UBSan smoke testing passed on an earlier host iteration.

NOT RUN: complete repository configure/build, full ctest, noqeri doctor/audit, Windows/macOS builds, PostgreSQL server integration, sudden power-loss recovery, or long-run fuzzing. No v1.0.0 release tag should be created until these gates pass.
