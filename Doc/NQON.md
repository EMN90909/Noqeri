# NQON

**NQON** means **Noqeri Object Notation**. It is the portable human-readable data and configuration format used by the Noqeri ecosystem.

NQON is deliberately independent from the Noqeri programming language. A Python, JavaScript, Go, Rust, Java, C#, C or C++ program can parse `.nqon` by implementing the public grammar without embedding the Noqeri compiler.

## Design target

The common case should look like this:

```nqon
name = Aroeno
version = "1.0.0"
private = true

packages {
    forge = "noqeri/forge@1.2.0"
    postgres = "noqeri/postgres@2.0.0"
}
```

Compared with JSON, NQON removes mandatory quoted keys, colons and commas. Compared with YAML, indentation does not define structure. Braces define nested objects, so moving whitespace does not silently change the data tree.

## Values

```nqon
name = Noqeri
version = "1.0.0"
enabled = true
workers = 4
ratio = 0.75
nothing = null

targets = [
    linux
    windows
    macos
]

server {
    host = localhost
    port = 8080
}
```

Quote a string when it contains spaces, punctuation outside the bare-string grammar, looks numeric, or would otherwise be ambiguous.

## Comments

Only `//` line comments are part of NQON 1.0:

```nqon
// Development server
port = 8080
```

## Standalone reference tool

The reference tool does not load or compile Noqeri source:

```sh
node Runtime/nqon-cli.mjs check project.nqon
node Runtime/nqon-cli.mjs to-json project.nqon
node Runtime/nqon-cli.mjs from-json package.json
```

The converter is a bootstrap convenience, not a requirement of the format. Native libraries in other languages should implement the grammar directly.

## Project manifests

New Noqeri projects use `project.nqon`. During the migration, ecosystem tooling can still read legacy `project.noqeri` manifests so existing projects and registry artifacts do not break.

A typical project manifest is:

```nqon
name = my-app
version = "0.1.0"
edition = "2026"
entry = src/main.noqeri

packages {
    forge = "noqeri/forge@1.2.0"
}
```

Exact resolved package versions and integrity hashes remain in `noqeri.lock`.

## NQON and NQDB

`.nqon` is text for portable data and configuration. `.nqdb` is the database file format and should remain optimized for durable storage rather than human editing. Database tools may import/export NQON, but an NQDB file is not NQON with a different extension.
