# Noqeri ABI v2

Noqeri owns its ABI. It is not the Linux ABI, Windows ABI, macOS ABI, libc ABI, or a kernel-specific ABI.

The stable source language remains deliberately small. Platform capability is supplied below the language and NIR layers through `Include/noqeri/abi.h`, so Noqeri code does not need pointer operators, raw-memory instructions, architecture intrinsics, or layout-dependent records.

## Native entry

Freestanding x86-64 output exports:

```c
int64_t noqeri_entry(const noqeri_abi* abi);
```

There is no `_start`, process exit syscall, libc requirement, Linux loader assumption, or built-in OS startup sequence. The caller owns startup, stack setup, memory policy, interrupts, processes, and shutdown.

## ABI table

ABI v2 contains a Noqeri-defined table with:

- `write(context, data, size)` for byte output used by `print`;
- `clock_millis(context)` as an optional time source;
- `platform_name(context)` as an optional platform identity;
- named Noqeri service entries for higher-level integrations;
- tagged primitive values for the interpreter/embedding boundary;
- explicit `abi_version` and `struct_size` fields for compatibility.

The interpreter's desktop implementation is only one adapter. A kernel can supply a different `noqeri_abi` table without changing Noqeri source, NIR, or generated code.

## Native calling convention

Generated x86-64 Noqeri functions use a Noqeri-defined internal convention: the first six scalar arguments use `rdi`, `rsi`, `rdx`, `rcx`, `r8`, and `r9`; scalar results use `rax`.

That is part of the Noqeri native target contract, not a promise that the surrounding operating system uses the same convention. A Windows, macOS, firmware, bootloader, or kernel adapter may provide a shim at `noqeri_entry` when required.

## Freestanding build flow

`noqeri build source.noqeri build/program.s` emits freestanding assembly only. It deliberately does not run `as`, `ld`, or an OS linker automatically.

To assemble for a chosen object format, configure an adapter command:

```sh
export NOQERI_NATIVE_ASSEMBLER='clang -c {input} -o {output}'
noqeri assemble build/program.s build/program.o
```

A kernel may instead assemble and link the emitted file inside its own build using its own linker script and image format.

## Service model

The reference interpreter still supports ordinary service calls such as `print`, `clockMillis`, `platform`, and `textLength`, plus named services registered in `noqeri_abi.services`. The native backend directly supports the fixed ABI callbacks needed for freestanding execution; generic named-service marshalling remains an interpreter/embedding feature until its native lowering is implemented.

## Compatibility

Breaking changes to the ABI table, native entry contract, primitive representation, or calling convention require a new `NOQERI_ABI_VERSION`. New fields must be appended and guarded by `struct_size`.
