# Native Noqeri database-backed pages (experimental)

**NoeSQL is the canonical native Noqeri database**. Noqeri compiles/serves the web pages, but database statements are delegated to the actual NoeSQL engine from [EMN90909/NoeSQL](https://github.com/EMN90909/NoeSQL). SQLite, PostgreSQL, DuckDB and MySQL are explicit, optional native backend adapters. No browser JavaScript or Node.js is needed.

The compiler CLI `noqeri sql <NoeSQL-subcommand> [...]` now forwards to the real native NoeSQL executable selected by `NOESQL_BIN`. `noqeri db` remains **legacy NoqeriDB**, and does not read `.noedb` files.

## NoeSQL: default and authoritative syntax

Use the NoeSQL Rust/Zig runtime built from its own repository with the Zig pager enabled. Its canonical store is `.noedb` and its schema scripts use `.noe`. **Do not rename** legacy NoqeriDB `.nqdb`/`.nqd` or SQLite files.

Native NoeSQL grammar examples:

```noe
db visitors
table visitors {
  id: int key auto
  name: text required
}
visitors add { name: "Ada" }
visitors get id, name sort id
```

See `examples/native_web_noesql/setup.noe` for a schema and `main.noqeri` for UI. Noqeri does not parse this SQL-like language or translate it to another dialect. It passes the immutable NoeSQL statement and typed parameters to the native NoeSQL executable through `execvp` (no shell). A companion NoeSQL change adds `noesql exec <database.noedb> <source> name:text:value` and typed `int`/`bool`/`null` parameters. The NoeSQL engine owns validation and storage.

```sh
# Build noesql in its own repository (Rust + Zig pager).
export NOESQL_BIN=/absolute/path/to/noesql
mkdir -p noqeri-data
$NOESQL_BIN run noqeri-data/visitors.noedb examples/native_web_noesql/setup.noe
./build/noqeri-web serve-app examples/native_web_noesql 8080 noqeri-data/session.nqkv
```

## Database configuration

```noqeri
database {
  engine: "noesql"
  path: "noqeri-data/visitors.noedb"
}
query visitors = "visitors get id, name sort id take 20"
route "/" {
  main {
    p { visitors }
    form on.submit {
      db.execute("visitors add { name: $name }", name: form.name)
    } {
      input name: "name" type: "text" {}
      button type: "submit" { "Add" }
    }
  }
}
```

Values from form inputs are **parameters**, never concatenated into source. SQL engines use `:name` placeholders in trusted statements; NoeSQL uses its own `$name` syntax. The native view `query` result is currently presented as HTML-escaped JSON text, not as a typed row template.

| Backend | Source syntax | Connection | Native requirement |
| --- | --- | --- | --- |
| NoeSQL (default) | `.noe`: `visitors get name` | `path: "...noedb"` | `NOESQL_BIN` native executable, Zig pager |
| SQLite | SQL: `SELECT name FROM visitors` | `path: "...sqlite"` | SQLite3 library, detected by CMake |
| PostgreSQL | SQL: `SELECT name FROM visitors` | `dsn_env: "DATABASE_URL"` | libpq, detected by CMake |
| DuckDB | SQL: `SELECT name FROM visitors` | `path: "...duckdb"` | DuckDB native C library |
| MySQL / MariaDB | SQL: `SELECT name FROM visitors` | `dsn_env: "NOQERI_MYSQL_DATABASE"` | MySQL/MariaDB C library plus NOQERI_MYSQL_HOST, USER, PASSWORD |

SQLite example: `examples/native_web_sqlite/main.noqeri`. Create the table independently in a migration before starting the server. For PostgreSQL and MySQL, connection credentials should be supplied through environment variables, not committed to `.noqeri` files.

## Build and tests

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target noqeri-web
ctest --test-dir build -R noqeri-native-web --output-on-failure
python3 tests/native_web_database.py ./build/noqeri-web
```

CMake automatically enables native SQLite/libpq/DuckDB/MySQL providers only when both headers and libraries are found. Without a chosen provider, requests fail with an explicit error; **no SQLite or legacy NoqeriDB fallback** is allowed. No web app uses a browser JS runtime.

## Important current limits

- SQLite has a real database HTTP integration test: insert, select, escaping and injection resistance. NoeSQL subprocess invocation is tested with a **mock process** in Noqeri CI. Its CLI binding parser is separately tested in the NoeSQL repository; full real Noqeri -> NoeSQL -> Zig pager end-to-end qualification is still outstanding.
- PostgreSQL and MySQL clients are built where native packages exist, but live-server integration is not yet qualified. DuckDB needs its optional C SDK; feature parity across the five engines is not claimed.
- SQLite, PostgreSQL, DuckDB and MySQL execute **their own SQL dialects**, rather than accepting `.noe`. Query parameter placeholders are rewritten outside string literals, then passed into prepared native client calls. No dynamic identifiers, table names, or raw SQL fragments may be supplied from form fields.
- SQL adapter values and result cells currently use strings; SQL null is not represented distinctly. Queries return a bounded JSON string (256 rows, 64 fields, 64 KiB), and only read-only declarations are allowed in view definitions. The read-only source gate is a syntax check, **not a database authorization guarantee**.
- No cross-engine transaction coordinator exists. Each `db.execute` action is independent. Mixing DB writes with `.nqkv` persisted state in one action is disallowed. NoeSQL and the native Noqeri web server both remain experimental, with no production-grade auth, TLS or concurrency certification.
- The HTTP server remains **localhost-only**. The native `.noqeri` parser is still the current C++ bootstrap rather than full Stage-2 typed NIR compilation.

Further milestones: a direct NoeSQL native session ABI/FFI with bound types, transaction grouping and rollback, migration tooling, row iteration/templates, audited production HTTP/authz, real PostgreSQL/DuckDB/MySQL tests, and full real-NoeSQL integration CI.

## NoeSQL native session-v2 adapter (v0.1.4 developer preview)

Set `NOESQL_NATIVE_PROVIDER=/absolute/path/to/provider.so` to use in-process Rust-compatible session callbacks rather than one subprocess per query. Noqeri requires the provider to export `noesql_session_v2_get` returning the canonical callback table with `abi_version=2`. The adapter uses `open`, `prepare`, `query`, `execute`, `begin`, `commit`, `rollback`, `release` and `close`. Its Noqeri-specific wire profile passes bind values as a bounded UTF-8 JSON object of **text** parameters. An invalid configured provider fails closed; only an application with no provider configured uses the existing native CLI fallback.

The integration test uses a mock callback library to verify ABI calls, marshaling and the no-subprocess path. The separate NoeSQL Rust/Zig repository has not yet delivered a qualified real v2 provider for this profile; **do not assert full NoeSQL WAL/transaction correctness or broad production readiness from the mock**. The canonical NoeSQL engine remains owned by the NoeSQL repository, not the Noqeri web compiler.


## Cloud providers (experimental)

Native `noqeri-web` also supports server-only Supabase REST, Firebase Realtime Database REST, and Neon/PostgreSQL with verified TLS. Use the HTTPS `dsn_env` and `key_env` configuration; credentials must remain in server environment variables. See [cloud databases and reactive browser capabilities](WEB_CLOUD_REALTIME.md) for exact syntax, authorization limitations, and verification.
