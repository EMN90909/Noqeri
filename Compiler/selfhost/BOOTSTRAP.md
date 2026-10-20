# Noqeri bootstrap ownership

Noqeri compiler semantics live in `Compiler/selfhost/*.nqr`: lexical analysis, parsing, semantic analysis, generic inference, NIR lowering, optimization, safety checks, target policy and NQO2 serialization are Noqeri-owned.

## Native NQO2 bootstrap path

The default bootstrap no longer generates or imports a JavaScript compiler module. `scripts/build.sh` and `scripts/build.ps1` delegate to the native NQO2 bootstrap:

1. Build or locate `noqeri-nqo`, the native NQO2 execution host in `Runtime/native/nqo_vm.cpp`.
2. Load a trusted Stage-1 NQO2 compiler seed.
3. Require that seed to expose `stage2CompileToNqo`.
4. Merge the deterministic compiler source closure from `Compiler/selfhost/stage2-sources.txt`.
5. Execute the Noqeri-owned compiler inside the native NQO2 host to produce `noqeri-stage2.nqo`.
6. Execute Stage 2 in the same native host to rebuild `noqeri-stage3.nqo`.
7. Reject the bootstrap unless Stage 2 and Stage 3 are byte-identical.

The compiler export `stage2CompileToNqo` owns parse -> semantic analysis -> NIR lowering -> optimization -> validation -> NQO2 serialization. The C++ host decodes and executes the stable NQO2 instruction format; it does not implement the compiler frontend.

### Windows

No WSL is required.

```powershell
$env:NOQERI_STAGE1_NQO = "C:\path\to\trusted-noqeri-stage1.nqo"
.\bootstrap.ps1
```

The script builds `noqeri-nqo.exe` with CMake when a prebuilt runtime is not supplied, then writes `build/native-selfhost-proof.json` after Stage-2/Stage-3 equivalence succeeds.

### Linux and macOS

```sh
NOQERI_STAGE1_NQO=/path/to/trusted-noqeri-stage1.nqo ./bootstrap.sh
```

A prebuilt native host can be supplied with `NOQERI_NQO_RUNTIME`. `NOQERI_TARGET` can override automatic target selection.

## Trust boundary

A trusted first NQO2 seed is still required. The repository intentionally does not claim that an arbitrary older seed is sufficient: the seed must implement the current bootstrap ABI, including `stage2CompileToNqo`.

The current native bootstrap host is written in C++17. Therefore the compiler can now rebuild its Noqeri-owned compiler semantics without Node.js, but a fully Noqeri-only release is **not yet proven**. Removing the remaining C++ bootstrap trust boundary requires either a previously verified `noqeri-nqo` binary with reproducible provenance or a Noqeri-native implementation of the NQO2 execution host followed by another equivalence bootstrap.

`Runtime/nqo-vm.mjs` is retained only as a reference/parity implementation during migration. It is not the default compiler bootstrap path.

## Verification policy

Self-hosting is accepted only when the executable chain is evidenced:

- trusted Stage 1 executes successfully in the native NQO2 host;
- Stage 1 builds Stage 2 from the checked compiler source closure;
- Stage 2 rebuilds Stage 3 from that exact source closure;
- Stage 2 and Stage 3 NQO2 bytes are identical;
- the native host self-test passes;
- the required compiler/runtime conformance suites pass before release.

The older `scripts/bootstrap-stage2.mjs` proof runner remains useful for broader release-suite orchestration, but JavaScript execution is no longer the mechanism that rebuilds the compiler.

Cross-language interoperability is a separate contract. It uses the versioned embedding C ABI, experimental native FFI bindings and browser/host adapters documented in `Doc/ABI.md` and `Doc/FFI.md`.
