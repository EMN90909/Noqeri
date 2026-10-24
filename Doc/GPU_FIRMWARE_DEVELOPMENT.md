# Noqeri GPU firmware development profile (experimental)

Noqeri now includes a **development-only** GPU control and firmware experiment
profile. It is appropriate for learning firmware control flow, prototyping
device-specific register logic in a privileged simulated environment and
generating reproducible **simulation artifacts**. It is **not** firmware
for NVIDIA, AMD, Intel, Apple, or any other physical GPU.

## Scope and supported targets

| Feature | Status |
|---|---|
| Noqeri native x86-64 MMIO register code | Native compiled and mock-tested |
| Checked 32-bit volatile read/write | Supported for a trusted mapped window |
| Bounded status polling | Supported |
| Four-word toy command mailbox | Supported in isolated simulator |
| Tamper-detecting source/payload development package | Supported |
| Real GPU firmware instruction set/ABI | Not supported |
| Device-specific register map, interrupts, DMA, boot ROM | Not supported |
| Code-signing, firmware trust chain, secure boot | Not supported |
| Driver integration or physical firmware flashing | Not supported |

The test target is **`sim-x86_64` only**. Production GPU firmware uses
vendor-specific processors, image formats, hardware memory ordering and often
cryptographic vendor signing. Compiling freestanding Linux x86-64 code cannot
make firmware for a GPU processor.

## Compile and verify the Noqeri module

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target noqeri -j2
bash tests/scripts/gpu-firmware-sim-e2e.sh build/noqeri
```

The exported ABI in `Lib/std/gpu_firmware.noqeri` is:

- `nqGpuFwRead32(registers: *volatile u32, words, index, output: *u32)`
- `nqGpuFwWrite32(registers: *volatile u32, words, index, value)`
- `nqGpuFwPollEqual32(registers: *volatile u32, words, index, expected, attempts)`
- `nqGpuFwSimStep(registers: *volatile u32, words)`

All APIs reject null pointers, unsupported window sizes and out-of-range
word indexes before reading or writing a register. Polling is capped at
4096 iterations. There is no way to prove from a raw pointer that
hardware has actually mapped writable storage; the OS or hypervisor must
establish and own that mapping. A false caller-supplied window length
still permits out-of-bounds access, so **untrusted callers must never
supply raw MMIO pointers**.

MMIO is not ordinary RAM. Volatile preserves individual load/store
effects, but it does **not** establish the bus ordering, register
readback requirements, cache coherency, atomic device semantics or DMA
barriers of an arbitrary GPU. Device-specific hardware code must define
those rules before deployment. Do not treat the toy command values as
real PCI IDs or vendor GPU register offsets.

## Firmware development evidence envelope

Use the offline packer to bundle Noqeri source and a simulator payload.
The `.bin` input is an arbitrary simulation byte sequence, not a GPU ROM:

```sh
node scripts/gpu-firmware-package.mjs pack \
  examples/gpu_firmware_sim.noqeri build/gpu-sim.bin build/gpu-sim.nqfw.json
node scripts/gpu-firmware-package.mjs verify build/gpu-sim.nqfw.json
```

The input source and payload each have a one-MiB maximum. The output uses
a strict JSON schema, stores the payload in base64 and includes SHA-256
digests for the source and payload. Packaging refuses to overwrite an
existing file and verification rejects mutated payloads, unknown formats,
bad target IDs and malformed encodings. It cannot prove that the payload
was compiled from the source or that a publisher is trusted.
**SHA-256 integrity checks are not authentication or code signing.**

There is deliberately no `flash`, `upload-to-device`, driver-binding,
kernel sysfs or PCI-BAR writing operation. On Linux, real firmware
deployment ordinarily proceeds through a device-specific kernel driver
and its firmware-loading contract. For that work use the
[Linux firmware loader documentation](https://docs.kernel.org/driver-api/firmware/request_firmware.html).

## Acceptance gates for physical GPU firmware development

A real device target must have documented processor ISA, firmware memory
layout, boot protocol, signed image format, OS-driver handoff and vendor
redistribution authorization. Add emulated instruction-set tests,
fuzzed binary parsing, reproducible toolchain qualifications and a
recoverable hardware test station before any real device loading.
There is no evidence yet for any of those physical-device gates.
