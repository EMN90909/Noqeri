# Testing strategy

noqeri uses layers rather than one monolithic test command.

1. **Repository verification** checks required project structure, public naming, brand assets and the absence of legacy `.noe`/`.ric` source files.
2. **CTest smoke tests** verify version reporting, parsing/type checking, the `.noqeri` test runner and `noqeri doctor`.
3. **Language conformance seeds** in `tests/` exercise arithmetic, functions and control flow through the reference interpreter.
4. **Native smoke tests** on Linux x86-64 compile and execute a native `.noqeri` program.
5. **Sanitizer CI** builds the compiler with ASan/UBSan on Linux and runs the core CTest suite.
6. **Fuzzing** is staged under `Tools/fuzz/` and must be expanded before a stable production release.
7. **Benchmarks** live in `Benchmarks/` and are kept separate from correctness tests so performance regressions can be tracked without weakening conformance requirements.

A syntax or semantic change is incomplete unless grammar/reference docs and relevant tests change together.
