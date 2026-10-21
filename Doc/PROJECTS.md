# noqeri projects

Project metadata uses NQON, Noqeri's portable configuration/data format.

```nqon
name = demo
version = "0.1.0"
entry = src/main.noqeri
profile = app
target = native
```

The canonical file is `project.nqon`. `noqeri new demo` creates the manifest, `src/main.noqeri`, a tests directory and `noqeri.lock`. `noqeri lock` writes deterministic dependency lines using the bootstrap lock format.

Packages live in a simple object:

```nqon
packages {
    forge = "noqeri/forge@1.2.0"
}
```

Legacy `project.noqeri` manifests remain readable during migration, but new tooling and documentation should not generate them.
