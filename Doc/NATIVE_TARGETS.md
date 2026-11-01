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

## Floating-point native casting candidate (2026-11-01)

The Stage-0 Linux x86-64 emitter now distinguishes numeric conversion from
bit reinterpretation in explicit `as` casts. It emits SSE2 signed integer/
binary64 conversion and bounded truncating binary64/integer conversion.
Unsigned 64-bit values are handled without treating their high bit as a sign
bit. Non-finite values and float-to-integer values outside the target range
trap instead of returning x86's indefinite-conversion sentinel.

This does **not** make native code generation portable across architectures.
The differential test `tests/native_float_casts.noqeri` and invalid-conversion
fixtures must pass native and reference execution at -O0 and -O3 before the
new casts are described as qualified. All other native-target restrictions
remain in effect.
