# Noqeri bootstrap ownership

Noqeri compiler semantics live in `Compiler/selfhost/*.noqeri`: lexical analysis, parsing, semantic analysis, generic inference, NIR lowering, optimization, safety checks, target policy and NQRI serialization are Noqeri-owned.

## Native NQRI bootstrap path

The default bootstrap no longer generates or imports a JavaScript compiler module. `scripts/build.sh` and `scripts/build.ps1` delegate to the native NQRI bootstrap:

1. Build or locate `noqeri-nqri`, the native NQRI execution host in `Runtime/native/nqri_vm.cpp`.
2. If a trusted Stage-1 NQRI seed is not supplied, build the C++ Stage-0 compiler and run its dedicated `bootstrap-seed` command against `Compiler/selfhost/main.noqeri`.
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

# Optional: override the reproducibly generated Stage-1 seed
$env:NOQERI_STAGE1_NQRI = "C:\path\to\trusted-noqeri-stage1.nqri"
```

The script builds `noqeri-nqri.exe` with CMake when a prebuilt runtime is not supplied, then writes `build/native-selfhost-proof.json` after Stage-2/Stage-3 equivalence succeeds.

### Linux and macOS

```sh
./bootstrap.sh

# Optional seed override:
NOQERI_STAGE1_NQRI=/path/to/trusted-noqeri-stage1.nqri ./bootstrap.sh
```

A prebuilt native host can be supplied with `NOQERI_NQRI_RUNTIME`. `NOQERI_TARGET` can override automatic target selection.

## Trust boundary

The missing-seed gap is closed by a reproducible Stage-0 path: the checked C++ compiler source builds the `noqeri` seed compiler, whose `bootstrap-seed` command produces Stage 1 from the Noqeri compiler sources. An externally supplied Stage-1 artifact is optional, not required.

The remaining trust boundary is therefore explicit rather than missing: the first local translation trusts the C++17 Stage-0 compiler and native NQRI execution host. Stage 1, Stage 2 and Stage 3 are then Noqeri compiler artifacts, and Stage 2/Stage 3 must be byte-identical. Removing C++ from the root of trust entirely requires a previously verified native Noqeri bootstrap executable or an independently reproduced seed, followed by the same equivalence gate.

`Runtime/nqri-vm.mjs` is retained only as a reference/parity implementation during migration. It is not the default compiler bootstrap path.

## Verification policy

Self-hosting is accepted only when the executable chain is evidenced:

- trusted Stage 1 executes successfully in the native NQRI host;
- Stage 1 builds Stage 2 from the checked compiler source closure;
- Stage 2 rebuilds Stage 3 from that exact source closure;
- Stage 2 and Stage 3 NQRI bytes are identical;
- the native host self-test passes;
- the required compiler/runtime conformance suites pass before release.

The older `scripts/bootstrap-stage2.mjs` proof runner remains useful for broader release-suite orchestration, but JavaScript execution is no longer the mechanism that rebuilds the compiler.

Cross-language interoperability is a separate contract. It uses the versioned embedding C ABI, experimental native FFI bindings and browser/host adapters documented in `Doc/ABI.md` and `Doc/FFI.md`.
