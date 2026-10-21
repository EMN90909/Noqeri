# Noqeri self-hosting model

Noqeri uses staged bootstrap with a strict provenance boundary.

## Stage 0 — archival seed

The historical C++17 compiler remains a Stage-0 dependency capable of generating a Noqeri Stage-1 candidate on a machine with no Noqeri compiler. Local Stage-0 output is not independently trusted merely because it was generated reproducibly. It is not the intended implementation language of the language semantics, and new compiler functionality belongs in `Compiler/selfhost/`.

Do not delete or relabel the seed merely because equivalent Noqeri source exists. It becomes unnecessary to normal releases only after the Stage-2 proof below passes on actual binaries.

## Stage 1 — Noqeri compiler source

`Compiler/selfhost/` contains the Noqeri-owned frontend, program parser, type/semantic analysis, generic inference, NIR, lowering, optimizer, target/ABI model, object-format writers, backend/link policy, security checks, formatter/LSP policy and Stage-2 proof contract. `Compiler/selfhost/main.noqeri` now exposes the full parse -> semantic analysis -> NIR -> optimization/validation pipeline; `Compiler/selfhost/bootstrap.noqeri` remains the broad bootstrap/conformance export surface.

`Compiler/selfhost/compiler-sources.txt` is the deterministic source closure used by bootstrap verification. New compiler modules must be added to that manifest.

## Host boundary

Filesystem, process, terminal, network and executable-page services may be supplied by a portable host adapter. Compiler language semantics, safety rules, NIR semantics and code-generation policy belong in `.noqeri`. A host adapter must not silently reimplement the frontend, type checker, optimizer or backend semantics in JavaScript or C++.

A portable host adapter is not evidence of self-hosting by itself. The compiler binary still has to be produced by Noqeri from the Noqeri compiler source.

## Stage-1 -> Stage-2 completion gate

Full self-hosting is reached only when all are true:

1. stage 0 builds stage 1;
2. stage 1 compiles the complete compiler source into stage 2;
3. stage 2 rebuilds the same source;
4. stage-2 and rebuilt-stage-2 outputs are byte-identical where reproducibility permits, or an explicitly configured semantic-equivalence verifier passes where platform metadata differs;
5. frontend, type/semantic analysis, NIR, optimizer, native/object writers, formatter, LSP and package-manager conformance suites pass without calling the archival C++ seed;
6. fuzz, race, memory, overflow, reproducible-build and practical-corpus gates pass on the self-hosted compiler;
7. the normal release process no longer requires CMake/C++.

The repository must not claim this gate passed until an actual Noqeri compiler binary executes these steps. In addition, a locally reproducible seed, a caller-supplied SHA-256 pin and Stage-2/Stage-3 equivalence alone do not independently attest Stage-1 trust. No independently attested Stage-1 seed is committed yet. Source ownership and architecture can move ahead of the bootstrap proof; documentation cannot substitute for that proof.

## Native-code self-host gate

NQRI self-reproduction and native-code self-hosting are separate claims. The current pinned Stage-1 -> Stage-2 -> Stage-3 chain produces `.nqri` and executes it through `noqeri-nqri`. Its proof therefore records `execution_model=nqri-vm`, `native_codegen=false`, and `native_selfhost=false`.

A native-code self-host claim requires all of these additional facts:

1. the Noqeri-written backend lowers the complete compiler NIR into target machine instructions;
2. the Noqeri-written object writer emits complete sections, symbols and relocations for the compiler artifact;
3. Stage 1 produces a native Stage-2 compiler executable or equivalent native library artifact;
4. Stage 2 runs without `noqeri-nqri` and rebuilds the compiler into native Stage 3;
5. Stage-2 and Stage-3 native outputs satisfy reproducible byte equality or a documented semantic-equivalence rule for platform metadata;
6. native ABI, linker, relocation, object validation and compiler conformance suites pass on every claimed target;
7. the proof record can truthfully set `native_codegen=true` and `native_selfhost=true`.

Until those gates pass, `scripts/bootstrap-native.*` must fail closed. The Stage-0 C++ AOT backend may still be used to measure standalone native program execution, but those measurements must record `self_hosted_compiler=false` and are not proof of a native self-hosted compiler.

## Strict local verifier

The verifier is intentionally fail-closed:

```sh
cp Compiler/selfhost/bootstrap.example.json Compiler/selfhost/bootstrap-local.json
# configure the actual local Stage-1 and Stage-2 compiler commands
node scripts/bootstrap-stage2.mjs
```

It writes `build/stage2-proof.json` and exits zero only when both compiler generations succeed, output equivalence succeeds, every required conformance/hardening suite succeeds, and `release_needs_cpp` is explicitly `false`.

The example configuration contains `null` commands and `release_needs_cpp: true`; therefore a fresh checkout cannot accidentally manufacture a passing proof. `scripts/bootstrap-stage2.mjs` also hashes the exact compiler source closure and records the two compiler artifact hashes.

A non-zero result means exactly that: Stage 2 is not verified, and bootstrap provenance must not be removed or described as unnecessary.

## Local verification

GitHub Actions are not required. Release evidence should be generated locally or on self-managed target machines and retain bootstrap artifact hashes, source-closure hashes, conformance results, fuzz/sanitizer/race results, benchmark evidence and native-object validation reports.
