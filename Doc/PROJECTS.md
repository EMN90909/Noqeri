# noqeri projects

Project metadata is source-like rather than TOML.

```nqr
project {
    name: "demo"
    version: "0.1.0"
    entry: "src/main.noqeri"
    profile: "app"
    target: "native"
}
```

The file is `project.noqeri`. `noqeri new demo` creates the manifest, `src/main.noqeri`, a tests directory and `noqeri.lock`. `noqeri lock` writes deterministic dependency lines using the bootstrap lock format.
