# PR #11 conflict reconciliation

This reconciliation merges the current main branch history with PR #11 while retaining its unique NoeSQL source and tests. In paths changed by both branches, **main is retained** to avoid overwriting subsequent security, build, package manager, and compiler changes. NoeSQL remains experimental and native-runtime parity has not been proven.

Unique added files: 22. Divergent shared files require later targeted semantic cherry-picks; they were deliberately not substituted wholesale. The NoeSQL source and test code has not passed a complete bootstrapped compiler run in this environment. No GitHub Actions used.
