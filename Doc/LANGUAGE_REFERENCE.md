# Noqeri language reference

Noqeri is a compact statically checked language with an application-friendly surface and explicit systems escape hatches. Ordinary code can use inferred variables, decisions, counted/conditional loops, functions, generic records, collections, files/data libraries and concurrency abstractions without first learning pointers or ABI machinery. Systems code can still use fixed-width types, raw pointers, volatile memory, atomics, FFI, intrinsics and constrained inline assembly.

## Variables and everyday types

```nqr
let count = 3
const name = "Noqeri"
let enabled: bool = true
```

`let` creates a variable and `const` creates a binding that cannot be reassigned. An explicit type annotation is optional when the initializer gives the compiler enough information.

The convenient everyday types are `int`, `float`, `bool`, `string`, `null` and `void`.

Portable-width integer types are:

- unsigned: `u8`, `u16`, `u32`, `u64`, `usize`;
- signed: `i8`, `i16`, `i32`, `i64`, `isize`.

`usize` and `isize` are pointer-sized. `int` is the ordinary general-purpose signed integer type.

## Decisions

Parentheses around an `if` condition are optional:

```nqr
if score >= 50 {
    print("pass")
} else {
    print("try again")
}
```

The parenthesized form remains accepted for compatibility.

## Loops

Noqeri has two simple beginner-facing loop ideas.

Use `repeat` when the number of iterations is the idea:

```nqr
let total = 0
repeat 4 {
    total = total + 3
}
```

`repeat count { ... }` evaluates the count once, converts it to `int`, creates compiler-private limit/index bindings and lowers to the existing `while` AST. Zero and negative counts execute zero iterations. There is no separate repeat runtime or NIR operation.

Use `while` when the condition is the idea:

```nqr
let remaining = 3
while remaining > 0 {
    remaining = remaining - 1
}
```

Parentheses around a `while` condition are also optional.

## Functions

```nqr
function square(value: int): int {
    return value * value
}
```

Parameters and return values are statically checked. `export function` exposes a symbol and `extern function` declares a symbol provided by the surrounding link/host.

## Records

```nqr
record User {
    id: u64
    active: bool
}
```

Records use declaration-order layout with natural alignment. Field offsets are resolved during type checking.

A pointer to a record uses the same field syntax:

```nqr
function deactivate(user: *User): void {
    user.active = false
}
```

No separate `->` operator is needed.

## Generics

Generic function and record syntax stays deliberately small:

```nqr
record Cell<T> {
    value: T
}

function identity<T>(value: T): T {
    return value
}
```

The stdlib uses generic aggregate types such as `Map<K,V>`, `Deque<T>`, `Heap<T>`, `PriorityQueue<T>`, `Future<T>` and `Iterator<T>`. Generic constraints such as `Copy`, `Eq` and `Ord` express the small capabilities an operation needs rather than exposing a large trait language to beginners.

Call-site type arguments are normally inferred from values. Aggregate specializations are produced predictably by the compiler's generic/monomorphisation pass and are covered by the generic-record/collection maturity tests.

## Arrays and slices

Fixed arrays use `[T; N]`:

```nqr
let values: [u32; 4] = [1 as u32, 2 as u32, 3 as u32, 4 as u32]
print(values[2])
```

Array literals infer one element type. Noqeri does not silently narrow an ordinary integer into fixed-width storage where that conversion is not assignable; cast when width matters.

Slices use `[]T` and carry data plus length:

```nqr
let view: []u32 = slice(values)
print(len(view))
print(view[0])
```

A slice can also be created from raw storage:

```nqr
let view: []u8 = slice(bufferPointer, bufferLength)
```

Dynamic safe indexing lowers through explicit runtime bounds checks when it cannot be rejected/proven statically. Fixed arrays and slice descriptors require no hidden heap allocation in the freestanding model.

## Modules and imports

A source file can declare an organizational module name:

```nqr
module graphics.raster
```

Files are imported by relative path:

```nqr
import "math.noqeri"
```

File-backed compiler commands recursively resolve imports relative to the importing file, deduplicate loaded files and compile the resulting declaration graph together.

## Lightweight error propagation

Low-level/freestanding functions can use integer status propagation without exceptions or allocation:

```nqr
function readValue(): isize {
    if device_failed {
        throw 3
    }
    return 42
}

function caller(): isize {
    let value: isize = try readValue()
    return value
}
```

`throw code` converts a non-negative error code into a negative status and returns. `try expression` propagates a negative status from the current function. Higher-level libraries can layer more descriptive result models over this cheap boundary.

## Safe code and `unsafe`

Ordinary arrays/slices are checked. Operations that bypass ordinary memory guarantees must be visually explicit:

```nqr
let value: u32 = 42 as u32
let raw: *u32 = &value

unsafe {
    raw[0] = 7 as u32
}
```

`unsafe` grants access to the operation; it does not globally disable compiler analysis. Unsafe regions should stay small and normal callers should receive checked abstractions.

The current safety suite separately exercises runtime dynamic bounds failure, null raw access, mandatory unsafe boundaries, aggregate/interprocedural borrow escape and checked-overflow diagnostic execution. These are implementation/test facts, not a claim that every backend and aliasing pattern has already reached complete Rust-equivalent proof coverage.

## Pointers

`*T` is a pointer to `T`; `&value` produces an address; unary `*` dereferences. Pointer indexing performs scaled address arithmetic using the size of `T` and belongs inside the explicit unsafe boundary when it bypasses normal checked aggregate access.

Explicit pointer/integer conversion uses `as`:

```nqr
let address: usize = 0x1000
let mmio: *u32 = address as *u32
let raw: usize = mmio as usize
```

Implicit integer-to-pointer conversion is not performed.

## Volatile memory

Volatile is attached to the pointer:

```nqr
let status: *volatile u32 = 0xF0000000 as *volatile u32
```

Loads/stores through `*volatile T` lower to volatile NIR memory operations. `volatile` is an access property, not inter-thread synchronization.

## Atomics

Atomic operations are typed calls:

```nqr
let counter: u64 = 0 as u64
let p: *u64 = &counter

atomic.store(p, 1 as u64)
let current: u64 = atomic.load(p)
let old: u64 = atomic.exchange(p, 2 as u64)
let observed: u64 = atomic.compareExchange(p, 2 as u64, 3 as u64)
atomic.fence()
```

The bootstrap supports integer, boolean and pointer values up to eight bytes for these primitives. The default ordering is sequential consistency in the current implementation.

## CPU intrinsics

Architecture-specific operations use an explicit named escape hatch:

```nqr
intrinsic("x86.pause")
let cycles: u64 = intrinsic("x86.rdtsc")
intrinsic("compiler.fence")
```

Unsupported intrinsic names are compile-time errors rather than hidden host calls.

## Inline assembly

```nqr
asm("nop")
```

The current x86-64 backend restricts this escape hatch to one instruction string and rejects labels, newlines and assembler directives. The reference interpreter does not attempt to emulate arbitrary machine instructions.

## Casts

`value as Type` is the explicit cast form. Supported conversions include numeric casts, pointer-to-pointer casts, `usize`/`isize` to pointer, pointer to `usize`/`isize`, and `null` to pointer when allowed by the type checker.

## Linkage and ABI

```nqr
extern function device_write(data: *u8, count: usize): isize

export function library_entry(data: *u8, count: usize): isize {
    return device_write(data, count)
}
```

`extern` and `export` are language linkage features; neither implies an operating system. Friendly host services such as printing/time/platform can be provided through the separate Noqeri ABI.

## NIR model

The lower-level NIR keeps important semantics explicit, including address-of, width-carrying memory access, volatile access, pointer offsets, stack allocation, slice construction/data/length, casts, dynamic bounds/non-null checks, atomics, intrinsics, inline assembly and try/throw flow. `repeat` is intentionally absent from this list because it is already reduced to ordinary control flow in the parser.

See `Doc/ABI.md`, `Doc/FFI.md`, `Doc/FREESTANDING.md` and `Doc/NATIVE_TARGETS.md` for systems/embedding details.
