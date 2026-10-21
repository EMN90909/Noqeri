# Noqeri bootstrap ownership

Noqeri compiler semantics live in `Compiler/selfhost/*.noqeri`: lexical analysis, parsing, semantic analysis, generic inference, NIR lowering, optimization, safety checks, target policy and NQRI serialization are Noqeri-owned.

## Native NQRI bootstrap acceptance chain

The default bootstrap does not generate or import a JavaScript compiler module. `scripts/build.sh` and `scripts/build.ps1` delegate to the native NQRI bootstrap and enforce one concrete acceptance chain:

1. Build or locate `noqeri-nqri`, the native NQRI execution host in `Runtime/native/nqri_vm.cpp`.
2. Load the committed Stage-1 compiler from `Compiler/selfhost/trusted-stage1.nqri` and its digest from `Compiler/selfhost/trusted-stage1.sha256`.
3. Require that digest to match the canonical identity in `Compiler/selfhost/stage1-seed.sha256`, then SHA-256 the committed binary itself. Stage 1 is never executed unless all three identities agree.
4. Require the verified Stage-1 seed to export `stage2CompileToNqri`.
5. Execute Stage 1 in the native NQRI host to build `noqeri-stage2.nqri` from the deterministic source closure in `Compiler/selfhost/stage2-sources.txt`.
6. Execute Stage 2 against the exact same source closure to build `noqeri-stage3.nqri`.
7. Reject the bootstrap unless Stage 2 and Stage 3 are literally byte-for-byte identical. SHA-256 values are recorded as diagnostics and evidence, but hash equality alone is not the acceptance test.
8. In CI, independently rebuild Stage 1 with GCC-built and Clang-built Stage-0 compilers and require both outputs to be byte-for-byte identical to the committed Stage-1 file. Stage-0 is therefore a reproducibility checker, not the default bootstrap root.

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

The default path uses the committed Stage-1 binary only after verifying its repository digest and canonical SHA-256 pin. The script writes `build/native-selfhost-proof.json` only after the pin and Stage-2/Stage-3 byte-equality gates succeed.

### Linux and macOS

```sh
./bootstrap.sh

# Optional external seed override:
NOQERI_STAGE1_NQRI=/path/to/audited-stage1.nqri \
NOQERI_STAGE1_SHA256=YOUR_AUDITED_SHA256 \
./bootstrap.sh
```

A prebuilt native host can be supplied with `NOQERI_NQRI_RUNTIME`. `NOQERI_TARGET` can override automatic target selection. Stage-0 is not required for the default bootstrap; CI rebuilds it only to prove the committed seed remains reproducible.

## What the pin proves

The repository now has a canonical Stage-1 identity. A locally produced Stage-1 binary is rejected unless it exactly reproduces the bytes identified by the committed SHA-256 pin. CI additionally requires GCC and Clang Stage-0 builds to reproduce that same pin, so changing compiler sources without deliberately refreshing the seed pin fails the bootstrap gate.

This is stronger than accepting an arbitrary freshly generated Stage-1 candidate, but it is not the same claim as independent external attestation. `stage1_independently_trusted=false` remains explicit in bootstrap proof records. A separately audited or signed Stage-1 binary can still be supplied with `NOQERI_STAGE1_NQRI` plus its explicit `NOQERI_STAGE1_SHA256`.

## Verification policy

Self-hosting is accepted only when the executable chain is evidenced:

- `Compiler/selfhost/trusted-stage1.nqri` is committed and its bytes hash to the digest in both `trusted-stage1.sha256` and `stage1-seed.sha256` before Stage 1 executes;
- the Stage-1 pin metadata and committed binary pass `tests/bootstrap_seed_pin.test.mjs`;
- CI rebuilds Stage 1 with independent GCC and Clang Stage-0 builds and byte-compares both outputs to the committed seed;
- Stage 1 builds Stage 2 from the checked compiler source closure;
- Stage 2 rebuilds Stage 3 from that exact source closure;
- Stage 2 and Stage 3 NQRI files are byte-for-byte identical;
- the native host self-test passes;
- CI verifies the pinned Stage-1 bytes are reproducible with both GCC and Clang Stage-0 builds;
- required compiler/runtime conformance suites pass before release.

`Runtime/nqri-vm.mjs` remains only as a reference/parity implementation during migration. It is not the default compiler bootstrap path.

The older `scripts/bootstrap-stage2.mjs` proof runner remains useful for broader release-suite orchestration, but JavaScript execution is no longer the mechanism that rebuilds the compiler.

Cross-language interoperability is a separate contract. It uses the versioned embedding C ABI, experimental native FFI bindings and browser/host adapters documented in `Doc/ABI.md` and `Doc/FFI.md`.
