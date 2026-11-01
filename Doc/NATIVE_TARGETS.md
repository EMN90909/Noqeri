# Native target model

The self-hosted compiler has Noqeri-owned target, ABI, object-format and optimisation-profile policy modules under `Compiler/selfhost/`.

## First-class target identifiers

- Windows x86-64 — PE/COFF, Win64 ABI
- Windows ARM64 — PE/COFF, Windows ARM64 ABI
- Linux x86-64 — ELF64, System V AMD64 ABI
- Linux ARM64 — ELF64, AAPCS64
- macOS x86-64 — Mach-O 64, Darwin/System V x86-64 ABI rules
- macOS ARM64 — Mach-O 64, Apple ARM64 ABI

The target model defines pointer size, stack alignment, argument-register counts, red-zone/shadow-space behavior, object machine codes, relocation families, header sizes and alignment policy.

## Output kinds and profiles

Link policy has executable, object-only, static-library and shared-library modes. Profiles are `debug`, `release`, `size` and `speed`, with explicit optimisation/debug-symbol/strip/LTO policy.

## Cross-compilation

Target selection is data-driven rather than selected from the build host. This makes cross-compilation an architectural property once each encoder/linker is implemented and independently validated.

## Verification gate

The current format modules define policy and constants; they are not yet proof of complete binary emitters. Stable native support requires fixtures that parse emitted objects with independent platform tools and execute/link them on each target architecture. The project can gather that evidence on local or self-managed runners without GitHub Actions.

## Verified Linux x86-64 floating-point casts and printing (2026-11-01)

The Stage-0 Linux x86-64 emitter now distinguishes numeric conversion from
bit reinterpretation in explicit `as` casts. It emits SSE2 signed integer/
binary64 conversion and bounded truncating binary64/integer conversion.
Unsigned 64-bit values are handled without treating their high bit as a sign
bit. Non-finite values and float-to-integer values outside the target range
trap instead of returning x86's indefinite-conversion sentinel.

Hosted x86-64 AOT also supports `print(float)` using bounded six-decimal
formatting through libc `snprintf` followed by the versioned Noqeri ABI write
callback; the native/reference output parity suite includes signed zero.
This implementation is **hosted**, not suitable for freestanding targets
without an alternative formatting implementation.

The differential test `tests/native_float_casts.noqeri`, invalid-conversion
fixtures and `tests/native_float_print.noqeri` passed native and reference
execution at -O0 and -O3 in [GitHub CI](https://github.com/EMN90909/Noqeri/actions/runs/41280755031).
This does **not** make native code generation portable across architectures.
All other native-target restrictions remain in effect.

## Portable native C11 AOT bridge (experimental; 2026-11-01)

`noqeri emit-c source.noqeri build/source.c -O0` lowers validated scalar
NIR to C11 source. A C11 toolchain then produces **native machine code**,
without a Noqeri NQRI virtual machine. For example:

```sh
noqeri emit-c tests/portable_native_scalar.noqeri build/demo.c -O0
cc -std=c11 -O2 build/demo.c -lm -o build/demo
./build/demo
```

This backend currently handles scalar functions, integer/f64 arithmetic
and casts, boolean expressions, branches, loops and scalar printing.
Arrays, pointers, slices, aggregates, host effects, native intrinsics,
closures, inline assembly and atomics are rejected with a diagnostic
rather than silently interpreted. It does not replace the direct x86-64
assembler backend.

The emitted C11 is portable **source**; a host C compiler and linker must
produce the actual native machine code. It is not a standalone
ELF/PE/Mach-O encoder, cross-architecture codegen, native selfhosting,
or proof of every host's ABI. Cross-platform tests are required before
describing any platform as qualified.
