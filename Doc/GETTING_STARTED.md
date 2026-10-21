# Getting started

Noqeri source files end in `.noqeri`. The normal application path is deliberately small: values, decisions, counted repetition, functions, records and libraries first; systems mechanisms later.

## First program

Use an official Noqeri release or a previously built compiler for your platform:

```sh
noqeri --version
noqeri check hello.noqeri
noqeri run hello.noqeri
```

Save this as `hello.noqeri`:

```nqr
function square(x: int): int {
    return x * x
}

print("Hello from Noqeri")
print(square(7))
```

Semicolons are optional in ordinary source. `if` and `while` do not require condition parentheses.

## Repeat something a known number of times

For the common beginner case “do this N times”, use `repeat` instead of manually creating a counter:

```nqr
let total = 0

repeat 4 {
    total = total + 3
}

print(total) // 12
```

`repeat count { ... }` evaluates `count` once, requires an integer count, and executes the block exactly that many times when the value is positive. Zero or negative counts execute the block zero times. It lowers to the existing checked control-flow model, not a separate runtime or concurrency feature.

Use `while` when the stopping condition is not simply a count:

```nqr
let remaining = 3
while remaining > 0 {
    print(remaining)
    remaining = remaining - 1
}
```

This gives beginners two predictable loop ideas: **repeat N times** and **keep going while a condition is true**.

## Projects

`noqeri new` creates a `project.nqon` manifest. NQON is independently specified and can be parsed by non-Noqeri tools without embedding the compiler.

For a project, the normal commands are:

```sh
noqeri new my-app
noqeri check my-app
noqeri test my-app/tests
noqeri run my-app/src/main.noqeri
noqeri build my-app/src/main.noqeri
```

Application programmers should not need pointers, atomics, ABI layouts, object formats, assembly or FFI merely to use files, data formats, collections or ordinary concurrency abstractions. Those features remain available for systems work and interop.

## Standard-library mental model

Library names should describe real capabilities rather than placeholder namespaces. Current substantive foundations include collections/structures such as bitsets, Bloom filters, deque/heap/priority queue, ring/pool/cache; time primitives such as duration/calendar/clock; data-integrity and encoding primitives such as CRC/hash/endian/varint/encoding; and state utilities such as event/future/iterator/metrics/numeric/lexer/parser.

Many low-level structures use caller-owned storage so freestanding code does not secretly allocate. Beginner documentation should show the operation first (`push`, `get`, `set`, `take`, `complete`) and explain backing storage afterwards.

## Bootstrap from source

`main` is moving toward the staged self-hosting model documented in `Doc/SELF_HOSTING.md`. `Compiler/selfhost/` is the Noqeri-owned compiler source. The historical C++17 seed is bootstrap provenance, not the desired long-term compiler implementation.

Until the stage-1 -> stage-2 reproducibility gate in `SELF_HOSTING.md` has been demonstrated end-to-end on release machines, a source bootstrap may still require the archival host/bootstrap toolchain. Do not interpret that temporary bootstrap dependency as the normal language programming model.

Current local bootstrap scripts remain:

```sh
./scripts/build.sh
./build/noqeri --version
./scripts/test_maturity.sh
./scripts/test_safety.sh
```

On Windows use `scripts/build.ps1`; multi-config generators may place the executable at `build/Release/noqeri.exe`.

## Safety/diagnostic modes

The quality driver exposes opt-in heavy diagnostics while keeping normal Noqeri simple:

```sh
node Runtime/noqeri-quality.mjs run --check-memory hello.noqeri
node Runtime/noqeri-quality.mjs test --race tests
node Runtime/noqeri-quality.mjs test --overflow tests
```

`--check-memory` uses an AddressSanitizer/UBSan-instrumented host build; `--race` uses a ThreadSanitizer host build; `--overflow` enables Noqeri's checked integer execution path. Sanitizer presets currently require a host compiler/toolchain that supports the corresponding sanitizer.

Ordinary dynamic safe indexing is still checked independently of these heavy diagnostic modes. Raw operations that bypass normal guarantees belong in explicit `unsafe { ... }` blocks.

Noqeri should not claim performance leadership from anecdotes. See `Benchmarks/METHODOLOGY.md` for the evidence rules.
