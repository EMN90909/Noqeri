# NoeSQL 1.0 language candidate (not a stable database release)

The supplied NoeSQL design v0.1 is the normative direction. The `.noe` frontend is a simpler typed query language; `.nqr` remains Noqeri source, `.nqo` is portable compiled web/compiler output, and `.nqd` is the new NoeSQL snapshot format. A NoeSQL snapshot is **not** legacy `.nqdb`, SQLite, or PostgreSQL; there is no automatic migration between these formats.

## Example

```noe
db shop
table users {
 id: int key auto
 name: text required
 email: text unique required
 active: bool = true
}
index users(email)
users add {name: "Amina", email: "amina@example.com"}
users get name, email where active sort name take 10
users where email == "amina@example.com" set active = false
users where email == "amina@example.com" delete
```

## Offline execution and testing (no GitHub Actions)

The **reference** host runs with Node.js 22:

```sh
node Runtime/noesql/cli.mjs check tests/noesql_fixture.noe
node Runtime/noesql/cli.mjs run tests/noesql_fixture.noe ./demo.nqd
node --test tests/noesql.test.mjs
```

The Stage-2 CLI also delegates `noqeri noe check ...` and `noqeri noe run ...` to the same reference host.

## Current implementation

- Lexer with source offsets/line/column diagnostics, recursive-descent declarations/statements, precedence expression parser, AST.
- Catalog of tables, columns, primary/unique/required/nullable/default/reference rules, schema version counters, index metadata, named query validation.
- Separate semantic validation lowers to one shared IR; reference executor handles scan, projection, filtering, sorting, offset/limit, insert, update, delete.
- Native `.nqr` lexer and typed IR policy records under `Database/` establish a compiler-owned contract; they are not yet compiled into the running Stage-2 toolchain.
- `.nqd` JSON snapshots use SHA-256 for accidental corruption detection, exclusive writer directories, temporary writes and fsync/rename. This is *not* a WAL or MVCC engine. The hash is not a cryptographic authentication mechanism.
- Query parameters supplied separately to the execution API; no string interpolation.

## Missing for a stable release

This JS implementation is a **transitional reference runtime**, not a fully Noqeri-native compiler+engine. Named queries are validated/stored but not callable; indexes are catalog metadata, not accelerated seek structures; relational joins, full aggregates, migration DDL, complete null semantics, cross-dialect schema translation and typed `.nqr` query generation are not implemented. Snapshot crash-recovery and OS-level lock recovery require fault testing. Do not use this format for sensitive or critical production data.

SQLite and PostgreSQL interoperability uses separate existing host-native adapters; it has not been integrated with the NoeIR executor, and the PostgreSQL server integration is unverified.

## Compiler policy

The normal compiler build is self-hosted in `Compiler/selfhost/*.nqr` and emits `.nqo`. It needs a trusted Noqeri Stage-0 binary. Removing archived C++ bootstrap provenance before fixed-point compiler reproducibility would risk making recovery impossible. Retain the archived seed, keep the active build C++-independent, and remove the legacy tooling only when verified parity, target backends, ABI/FFI, tests and native/web code generation permit.

A powerful compiler for full-stack applications and OS programming additionally requires tested native targets, linker, memory/unsafe model, async I/O, TCP/TLS, process and filesystem APIs, and platform integration. This reference database engine does not establish any of those capabilities.

The existing compiler version string reads 1.0.0. **NoeSQL remains unreleased** until Stage-2 full build, .nqr-native frontend/executor, WAL/recovery, schema compatibility, performance and integration gates pass.
