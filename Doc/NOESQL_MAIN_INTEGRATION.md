# NoeSQL integration status

The NoeSQL experimental source modules, reference runtimes, WAL, adapters and tests from PRs #11 and #12 were integrated into main as **new files**. Existing compiler, safety, build and package-manager modules were **not overwritten**. Where PRs #11 and #12 introduced the same new file path, PR #12's revision was selected. The NoeSQL CLI remains an experimental component until integration and cross-engine tests pass.

This is source integration, **not proof that Noqeri-native .nqr NoeSQL modules compile or run**. The self-hosted Noqeri bootstrap has no verified seed in the current environment. The JavaScript reference behavior and native database are not interchangeable. Full release gates: clean build, grammar conformance, differential reference/native results, WAL crash recovery, transaction isolation, SQLite/PostgreSQL integration and fuzz/sanitizer tests. The standalone NoeSQL repository remains an independent reference and API-bridge project.
