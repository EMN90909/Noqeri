# Workspaces, conditional sources and offline builds

These tools keep platform conditions out of ordinary application source. They are stage-1 repository tooling today; native compiler integration should converge on the same small manifests rather than introducing a second syntax.

## Workspaces

A multi-package repository can declare `noqeri.workspace.json`:

```json
{
  "schema": 1,
  "members": ["packages/api", "packages/domain", "tools/migrate"]
}
```

Each member must contain `project.nqon` (legacy `project.noqeri` is accepted during migration). Run one public command over members:

```sh
node Tools/workspace/noqeri-workspace.mjs check
node Tools/workspace/noqeri-workspace.mjs test --continue
```

Workspace paths are relative to the workspace file and may not escape its root. The runner does not invent cross-package dependency semantics: package dependencies remain declared in each member manifest/lockfile.

## Conditional source selection

Keep target-dependent files in `noqeri.build.json`:

```json
{
  "schema": 1,
  "sources": ["src/common.noqeri"],
  "conditional": [
    {
      "when": {"os": ["linux"], "arch": ["x64", "arm64"], "features": ["tls"]},
      "sources": ["src/linux_tls.noqeri"]
    }
  ]
}
```

Inspect the selected sources:

```sh
node Tools/build/source-select.mjs noqeri.build.json --os=linux --arch=arm64 --feature=tls
```

Conditions are deliberately limited to OS, architecture, target and explicit feature names. This is preferable to a general compile-time programming language hidden inside build tags.

The current selector is a tooling contract; the native compiler does not yet consume `noqeri.build.json` directly. Until that integration lands, do not advertise conditional compilation as a compiler-native language feature.

## Vendoring and offline mode

The stage-1 package manager already content-addresses packages and re-hashes the cache. `noqeri vendor` extends that model by copying the **locked and verified** package set into `.noqeri/vendor-home/cache/registry-v1/...`.

Use the vendored home without network access:

```sh
NOQERI_HOME=.noqeri/vendor-home NOQERI_OFFLINE=1 noqeri check src/main.noqeri
```

`noqeri vendor --offline` only succeeds if every locked package is already present and passes cache integrity verification. Vendoring is not a second dependency resolver. The lockfile remains authoritative. Stage-1 vendoring currently requires lock v3 because the native v2 cache/checksum contract is different; v2 remains accepted by the audit path during migration.

## Dependency audit

The portable stage-1 CLI supports:

```sh
noqeri audit
NOQERI_OFFLINE=1 NOQERI_ADVISORY_FILE=.noqeri/advisories/index.json noqeri audit
```

Exit meanings are deliberate: `0` means advisory data was loaded and no locked dependency matched; `1` means an affected dependency exists; `2` means advisory state is unknown/unavailable or the audit input is invalid. An unavailable feed is never reported as “clean”.

The native bootstrap `noqeri audit` additionally scans source capabilities and lock integrity. During the bootstrap transition, both lock v2 (native package path) and lock v3 (portable stage-1 package path) are accepted and validated. A future migration should converge to one lock schema using an explicit migration tool rather than silently invalidating old projects.
