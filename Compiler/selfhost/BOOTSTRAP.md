# Noqeri bootstrap ownership

Noqeri compiler semantics live in `Compiler/selfhost/*.noqeri`: lexical analysis, parsing, semantic analysis, generic inference, NIR lowering, optimization, safety checks, target policy and NQRI serialization are Noqeri-owned.

## Native NQRI bootstrap path

The default bootstrap no longer generates or imports a JavaScript compiler module. `scripts/build.sh` and `scripts/build.ps1` delegate to the native NQRI bootstrap:

1. Build or locate `noqeri-nqri`, the native NQRI execution host in `Runtime/native/nqri_vm.cpp`.
2. If an externally pinned Stage-1 seed is not supplied, build the C++ Stage-0 compiler and run its dedicated `bootstrap-seed` command twice against `Compiler/selfhost/main.noqeri`, rejecting non-reproducible output.
3. Require the resulting Stage-1 seed to expose `stage2CompileToNqri` as an exported compiler ABI symbol.
4. Record seed provenance, including the Stage-0 compiler path/hash and Stage-1 seed hash, then merge the deterministic compiler source closure from `Compiler/selfhost/stage2-sources.txt`.
5. Execute the Noqeri-owned compiler inside the native NQRI host to produce `noqeri-stage2.nqri`.
6. Execute Stage 2 in the same native host to rebuild `noqeri-stage3.nqri`.
7. Reject the bootstrap unless Stage 2 and Stage 3 are byte-identical.

The compiler export `stage2CompileToNqri` owns parse -> semantic analysis -> NIR lowering -> optimization -> validation -> NQRI serialization. The C++ host decodes and executes the stable NQRI instruction format; it does not implement the compiler frontend.

### Windows

No WSL is required.

```powershell
.\bootstrap.ps1

# Optional: provide a separately audited Stage-1 seed and a pinned SHA-256
$env:NOQERI_STAGE1_NQRI = "C:\path\to\audited-stage1.nqri"
$env:NOQERI_STAGE1_SHA256 = "YOUR_AUDITED_64_DIGIT_SHA256"
```

The script builds `noqeri-nqri.exe` with CMake when a prebuilt runtime is not supplied, then writes `build/native-selfhost-proof.json` after Stage-2/Stage-3 equivalence succeeds.

### Linux and macOS

```sh
./bootstrap.sh

# Optional seed override:
NOQERI_STAGE1_NQRI=/path/to/audited-stage1.nqri NOQERI_STAGE1_SHA256=YOUR_AUDITED_SHA256 ./bootstrap.sh
```

A prebuilt native host can be supplied with `NOQERI_NQRI_RUNTIME`. `NOQERI_TARGET` can override automatic target selection.

## Trust boundary

**No independently attested Stage-1 seed is committed in this repository.** Local C++ Stage-0 compilation produces a reproducible *candidate*, not a separately verified root of trust. Stage-2/Stage-3 byte equality proves repeatability under the C++/native-host trust boundary but does not independently validate that boundary.

An external Stage-1 seed must exist and have an explicit `NOQERI_STAGE1_SHA256` pin. This guards against accidental replacement, but a caller-supplied hash is not signed provenance. Default local bootstrap regenerates its Stage-1 candidate and byte-compares two compilations instead of accepting an old build artifact. Proof records seed origin and digests and explicitly reports `stage1_independently_trusted=false`.

**To close the independent trust boundary:** reproduce and audit the `.nqri` Stage-1 binary using an independent, trusted build chain; commit the verified binary and digest with signed provenance and its target/runtime ABI; verify this package on a clean machine; run Stage 1 -> Stage 2 -> Stage 3 with full compiler/runtime conformance suites. No binary or attestation satisfying those criteria has been created in this change.

`Runtime/nqri-vm.mjs` is retained only as a reference/parity implementation during migration. It is not the default compiler bootstrap path.

## Verification policy

Self-hosting is accepted only when the executable chain is evidenced:

- a locally rebuilt or externally pinned Stage-1 candidate executes successfully in the native NQRI host, with independent seed trust assessed separately;
- Stage 1 builds Stage 2 from the checked compiler source closure;
- Stage 2 rebuilds Stage 3 from that exact source closure;
- Stage 2 and Stage 3 NQRI bytes are identical;
- the native host self-test passes;
- the required compiler/runtime conformance suites pass before release.

The older `scripts/bootstrap-stage2.mjs` proof runner remains useful for broader release-suite orchestration, but JavaScript execution is no longer the mechanism that rebuilds the compiler.

Cross-language interoperability is a separate contract. It uses the versioned embedding C ABI, experimental native FFI bindings and browser/host adapters documented in `Doc/ABI.md` and `Doc/FFI.md`.
