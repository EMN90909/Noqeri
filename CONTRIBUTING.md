# Contributing to noqeri

Keep the language contract, compiler, tests and documentation synchronized.

## Development flow

1. Build with CMake (`scripts/build.sh` or `scripts/build.ps1`).
2. Add or update `.noqeri` tests for language behavior.
3. Run `noqeri check`, `noqeri test`, and `noqeri doctor`.
4. Run the platform-appropriate release script before opening a pull request.
5. Update `Doc/` and `Grammar/` when user-visible syntax or semantics change.

Compiler implementation lives in `Compiler/bootstrap/` until a self-hosted noqeri compiler replaces the bootstrap. Public API changes belong in `Include/noqeri/`. Grammar changes must update `Grammar/noqeri.grammar.md`, tests and the language reference together.

Do not create empty placeholder modules. A new file must implement, test, configure or document a real responsibility.

Public naming is `noqeri`, `.noqeri`, `project.noqeri`, `noqeri.lock`, and the `noqeri` executable.
