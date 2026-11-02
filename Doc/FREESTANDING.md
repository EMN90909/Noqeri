# Freestanding Noqeri native target

Noqeri native code is defined independently of Linux and other operating systems.

## Entry contract

Freestanding x86-64 output exports `noqeri_entry(noqeri_abi*)`. It does not export `_start`, issue process syscalls, call libc, or terminate a process. The surrounding kernel, boot environment, application host, or OS adapter owns startup and shutdown.

## Noqeri ABI

`Include/noqeri/abi.h` is the Noqeri-owned ABI contract. A platform supplies callbacks for byte output, time, platform identity, and optional named services. Ordinary programs remain memory-safe by default. Trusted freestanding modules may explicitly opt into pointers, record layouts, raw memory operations and extern functions using `--unsafe`. Such code must validate firmware-owned pointers and object lifetimes.

## Object format and linking

The core compiler does not choose ELF, Mach-O, COFF/PE, a flat image, a kernel linker script, or another object format. `noqeri build` emits assembly. A target adapter may assemble/link that output using its own toolchain and linker script.

This keeps the native backend usable below an operating system as well as above one.
## Experimental x86-64 UEFI application target

`noqeri build Bootloader/uefi/noqeri/loader.noqeri build/loader.s --target=x86_64-unknown-uefi --unsafe -O2`
emits x86-64 **freestanding** assembly suitable for assembling to COFF with
`clang -target x86_64-pc-windows-msvc -c`. The UEFI target suppresses the
hosted `snprintf` float-print implementation and ELF-only GNU-stack directive.
Attempting native `print(float)` in firmware traps rather than pulling libc.
Noqeri does not invoke a linker, output a PE file by itself or supply UEFI
functions: an EFI target adapter must link a PE32+ EFI application with a
base-relocation directory, correct entry point and UEFI x64 calling convention.

**Important ABI boundary:** Noqeri's current generated functions still follow
its System V-style internal x86-64 ABI. UEFI requires the Microsoft x64 ABI
(RCX, RDX, R8, R9; 32 bytes of shadow space). Therefore the adapter must supply
a narrow native assembly entry/call bridge until full first-class EFIAPI
function annotations/calls are implemented. Do not call firmware function
pointers as ordinary Noqeri functions or mislabel the generated assembly as
Microsoft ABI. Never call Boot Services after a successful ExitBootServices.

The integration and OVMF smoke checks live in NoqeriOS under
`Bootloader/uefi/noqeri/`; the legacy gnu-efi path remains available.
This target is experimental and not physical-hardware certified.
