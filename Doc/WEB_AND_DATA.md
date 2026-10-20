# Web, server and data in Noqeri 1.0

Noqeri keeps these capabilities in libraries rather than adding special-purpose syntax.

## Artifact extensions

- `.noqeri` — Noqeri source.
- `.nqri` — portable Noqeri web object. It contains ECMAScript-module compatible output and should be served as `text/javascript`.
- `.nqd` — NoqeriDB data/schema script.
- `.nqdb` — durable NoqeriDB data file produced by the runtime.

## NoqeriDB

A `.nqd` script is intentionally smaller than SQL:

```nqd
database "app.nqdb"

table users {
    id: int key,
    name: text required,
    email: text unique
}

insert users { id: 1, name: "Ada", email: "ada@example.test" }
update users set { name: "Ada Lovelace" } where id = 1
select users where id = 1
```

The whole script is a transaction. Schema/type/unique/key validation occurs before the durable file is replaced. The first 1.0 engine is an embedded row store; it is not represented as a SQLite replacement in performance or feature breadth until benchmarks justify that claim.

## Web/server capability model

Browser-only facilities (DOM) and server-only facilities (filesystem, sockets, database drivers) are library capabilities. Portable `.nqri` code can use browser facilities directly and server facilities through a host runtime. This keeps the language semantics independent from Node, a browser, Linux or another specific environment.
