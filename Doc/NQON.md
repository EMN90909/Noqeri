# NQON: Noqeri Object Notation

NQON is a small, portable data and configuration format. Its file extension is `.nqon`.
It is part of the Noqeri ecosystem, but it is **not tied to the Noqeri compiler or runtime**. Any language can implement the grammar below, and the repository ships a standalone JavaScript reference implementation plus JSON conversion tooling.

The design goal is familiar object data without JSON's repeated quotes, commas and colons, and without indentation-sensitive structure.

## First look

```nqon
name = Aroeno
version = "1.0"
private = true

packages {
    forge = "1.2"
    postgres = "2.0"
}
```

The top level is an implicit object. `=` assigns a value. `name { ... }` is shorthand for `name = { ... }`.
Whitespace does not define nesting.

## Values

NQON has the same small data model expected from a portable object format:

- objects;
- arrays;
- strings;
- finite numbers;
- booleans (`true` and `false`);
- `null`.

Simple strings may be bare:

```nqon
name = Noqeri
entry = src/main.noqeri
target = x86_64-unknown-none
```

Quote a value when it contains whitespace, when its exact text looks like a number/boolean/null, or when punctuation could be ambiguous. Versions that must preserve exact textual identity should normally be quoted:

```nqon
version = "1.0"
```

## Objects and arrays

```nqon
server {
    host = localhost
    port = 8080
}

platforms = [linux windows macos]
```

The explicit object form is also valid:

```nqon
server = {
    host = localhost
    port = 8080
}
```

Commas are optional separators. Canonical formatting omits them.

## Comments

NQON uses the same comment spellings as Noqeri:

```nqon
// line comment
name = Noqeri

/* block comment */
private = true
```

## Portable grammar

```text
document    := objectEntries EOF
object      := "{" objectEntries "}"
objectEntry := key ("=" value | object)
key         := IDENT | STRING
value       := STRING | BARE | NUMBER | "true" | "false" | "null" | array | object
array       := "[" (value (","? value)*)? "]"
```

`IDENT` starts with an ASCII letter or `_` and then contains ASCII letters, digits, `_` or `-`.
A bare value ends at whitespace, `,`, `]`, `}`, `=`, `{`, `[` or the start of a comment.
Duplicate keys are errors. Numbers must be finite. A parser must not silently overwrite duplicate keys.

## Interoperability

The reference tool works without the Noqeri compiler:

```sh
node Tools/nqon.mjs check project.nqon
node Tools/nqon.mjs to-json project.nqon project.json
node Tools/nqon.mjs from-json project.json project.nqon
node Tools/nqon.mjs format project.nqon
```

Other languages should implement this specification directly. NQON is a data format, not a second programming language: it has no functions, imports, expressions, inheritance, executable hooks or install scripts.

## Project manifests

New Noqeri projects use `project.nqon`:

```nqon
name = demo
version = "0.1.0"
edition = "2026"
entry = src/main.noqeri
profile = app
target = x86_64-unknown-none
registry = "https://github.com/EMN90909/noqeri-registry"

packages {
    forge = "noqeri/forge@1.2.0"
}
```

`noqeri.lock` remains the authoritative exact dependency and integrity record. During the 0.7 migration, legacy `project.noqeri` manifests remain readable so existing projects do not break abruptly.
