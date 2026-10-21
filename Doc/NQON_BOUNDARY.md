# Noqeri Object Notation (.nqon): integration gate

`.nqon` is reserved for declarative configuration and object data. It is **not yet** a verified manifest/parser replacement. Do not rename current `project.noqeri` or published `package.noqeri` manifests to `.nqon` without grammar/parser, round-trip, security and migration tests.

Planned schema support: deterministic UTF-8 parse/serialize, repeated-key rejection, bounded nesting, size limits, booleans, semver/string handling, safe path validation, migrations from legacy project manifests, lockfile compatibility and forward-version rejection.

The Noqeri CLI remains the authority for `project.noqeri` until a Noqeri-native NQON parser and serializer pass compatibility suites. NQON must never silently parse an incompatible legacy database as a config document.
