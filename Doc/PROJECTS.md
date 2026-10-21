# Noqeri projects

New projects use the portable `project.nqon` manifest. The common case intentionally exposes only the settings a beginner owns:

```nqon
name = demo
version = "0.1.0"
```

The compiler supplies stable defaults for the entry file, edition, application profile, target and registry. A project only needs to spell those fields when it wants to override them:

```nqon
name = demo
version = "0.1.0"
entry = src/server.noqeri
target = x86_64-unknown-none

packages {
    forge = "noqeri/forge@1.2.0"
}
```

`noqeri new demo` creates:

```text
demo/
├── project.nqon
├── noqeri.lock
├── src/
│   └── main.noqeri
└── tests/
```

The starter `src/main.noqeri` is just:

```noqeri
print("Hello from Noqeri")
```

There is no required module declaration, explicit `main` function, type annotation or semicolon for the first program.

`noqeri lock` writes the deterministic lockfile. `noqeri add` and `noqeri install` update/resolve package information while `noqeri.lock` remains the exact dependency and integrity record.

NQON is a portable data format rather than executable Noqeri source. See `Doc/NQON.md`. Existing `project.noqeri` manifests remain readable during the 0.7 migration, but new tooling should generate `project.nqon`.
