# Noqeri bootstrap ownership

Noqeri compiler semantics live in `Compiler/selfhost/*.noqeri`: lexical analysis, parsing, semantic analysis, generic inference, NIR lowering, optimization, safety checks, target policy and NQRI serialization are Noqeri-owned.

## Native NQRI bootstrap acceptance chain

The default bootstrap does not generate or import a JavaScript compiler module. `scripts/build.sh` and `scripts/build.ps1` delegate to the native NQRI bootstrap and enforce one concrete acceptance chain:

1. Build or locate `noqeri-nqri`, the native NQRI execution host in `Runtime/native/nqri_vm.cpp`, and the C++ Stage-0 seed compiler.
2. Read the canonical Stage-1 SHA-256 from `Compiler/selfhost/stage1-seed.sha256` and its provenance from `Compiler/selfhost/stage1-seed.json`.
3. Run Stage-0 `bootstrap-seed` twice against `Compiler/selfhost/main.noqeri` and reject the build unless both Stage-1 candidates are byte-for-byte identical.
4. Hash the reproduced Stage-1 candidate and reject it unless its SHA-256 exactly matches the committed canonical pin. This check completes before the NQRI host is allowed to execute the seed.
5. Require the verified Stage-1 seed to export `stage2CompileToNqri`.
6. Execute Stage 1 in the native NQRI host to build `noqeri-stage2.nqri` from the deterministic source closure in `Compiler/selfhost/stage2-sources.txt`.
7. Execute Stage 2 against the exact same source closure to build `noqeri-stage3.nqri`.
8. Reject the bootstrap unless Stage 2 and Stage 3 are literally byte-for-byte identical. SHA-256 values are recorded as diagnostics and evidence, but hash equality alone is not the acceptance test.

The compiler export `stage2CompileToNqri` owns parse -> semantic analysis -> NIR lowering -> optimization -> validation -> NQRI serialization. The C++ host decodes and executes the stable NQRI instruction format; it does not implement the compiler frontend.

The canonical Stage-1 pin currently identifies:

```text
sha256 a5b308beafa7510c31edf5eec81782a42128f1add13779287c03212b30264bca
bytes  4124007
```

That exact seed was reproduced by both GCC-built and Clang-built Stage-0 compilers in GitHub Actions run `39585410281`. In the same accepted run, Stage 2 and Stage 3 were byte-identical and both hashed to `7fed1cfc89ae551c3c2eaa5b3309311ddbfe7a075d890e7df5ef06c9e7a0a409`.

### Windows

No WSL is required.

```powershell
.\bootstrap.ps1

# Optional: supply a separately audited Stage-1 seed.
# External seeds must always carry an explicit SHA-256 pin.
$env:NOQERI_STAGE1_NQRI = "C:\path\to\audited-stage1.nqri"
$env:NOQERI_STAGE1_SHA256 = "YOUR_AUDITED_64_DIGIT_SHA256"
.\bootstrap.ps1
```

The default path reproduces Stage 1 locally and verifies it against the repository pin. The script writes `build/native-selfhost-proof.json` only after the pin and Stage-2/Stage-3 byte-equality gates succeed.

### Linux and macOS

```sh
./bootstrap.sh

# Optional external seed override:
NOQERI_STAGE1_NQRI=/path/to/audited-stage1.nqri \
NOQERI_STAGE1_SHA256=YOUR_AUDITED_SHA256 \
./bootstrap.sh
```

A prebuilt native host can be supplied with `NOQERI_NQRI_RUNTIME`. A prebuilt Stage-0 compiler can be supplied with `NOQERI_STAGE0_CPP`. `NOQERI_TARGET` can override automatic target selection.

## What the pin proves

The repository now has a canonical Stage-1 identity. A locally produced Stage-1 binary is rejected unless it exactly reproduces the bytes identified by the committed SHA-256 pin. CI additionally requires GCC and Clang Stage-0 builds to reproduce that same pin, so changing compiler sources without deliberately refreshing the seed pin fails the bootstrap gate.

This is stronger than accepting an arbitrary freshly generated Stage-1 candidate, but it is not the same claim as independent external attestation. `stage1_independently_trusted=false` remains explicit in bootstrap proof records. A separately audited or signed Stage-1 binary can still be supplied with `NOQERI_STAGE1_NQRI` plus its explicit `NOQERI_STAGE1_SHA256`.

## Verification policy

Self-hosting is accepted only when the executable chain is evidenced:

- Stage-0 reproduces Stage 1 twice with byte-identical output;
- the reproduced Stage-1 SHA-256 matches `Compiler/selfhost/stage1-seed.sha256` before Stage 1 executes;
- the Stage-1 pin metadata passes `tests/bootstrap_seed_pin.test.mjs`;
- Stage 1 builds Stage 2 from the checked compiler source closure;
- Stage 2 rebuilds Stage 3 from that exact source closure;
- Stage 2 and Stage 3 NQRI files are byte-for-byte identical;
- the native host self-test passes;
- CI verifies the pinned Stage-1 bytes are reproducible with both GCC and Clang Stage-0 builds;
- required compiler/runtime conformance suites pass before release.

`Runtime/nqri-vm.mjs` remains only as a reference/parity implementation during migration. It is not the default compiler bootstrap path.

The older `scripts/bootstrap-stage2.mjs` proof runner remains useful for broader release-suite orchestration, but JavaScript execution is no longer the mechanism that rebuilds the compiler.

Cross-language interoperability is a separate contract. It uses the versioned embedding C ABI, experimental native FFI bindings and browser/host adapters documented in `Doc/ABI.md` and `Doc/FFI.md`.
