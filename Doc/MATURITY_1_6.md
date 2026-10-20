# Noqeri 1.6 maturity baseline

Noqeri 1.6 moves the project from a bootstrap-oriented language plus thin helper modules toward a coherent systems/application standard library. This document describes guarantees implemented in the compiler and bundled standard library. It intentionally distinguishes guarantees from low-level escape hatches.

## Aggregate generics

Generic records are specialised before type checking and lowering. Source code can define and consume records such as:

```nqr
record List<T> {
    data: *T
    length: usize
    capacity: usize
}

record Map<K, V> {
    keys: *K
    values: *V
    length: usize
    capacity: usize
}
```

Concrete uses such as `List<string>`, `Map<string, User>`, `Set<int>`, `Queue<Job>` and recursive pointer records such as `Node<T>` receive predictable specialised layouts. Function type arguments remain inferred at call sites and constrained generic functions continue to use the small `T: Eq + Copy` style rather than a trait system.

## Error model

The low-level ABI remains allocation-free: integer status values are non-negative for success and negative for failure. `throw code` encodes a non-negative code as `-(code + 1)` and `try` propagates that status without unwinding.

Application code can use `std/result` records (`Result<T>`, `Outcome<T>`, `Error`) to attach domain/message information while retaining the inexpensive ABI representation at system boundaries. This layer is explicit and does not introduce exceptions or hidden allocation.

## Memory and lifetime safety

The compiler rejects the following in ordinary checked source when it can prove them statically:

- returning pointers or slices borrowed from function-local storage;
- using a reference after the inner scope that owns its borrowed storage has ended;
- direct, provable null dereference/indexing;
- fixed-array accesses whose literal index is provably out of bounds;
- unsafe implicit integer narrowing/signedness conversions;
- mutation of `const` bindings;
- incompatible pointer assignments.

Safe initialization remains mandatory: local declarations require initialized values. Standard containers are caller-owned descriptors and validate length/capacity/null invariants rather than performing hidden allocation.

Raw pointers remain a systems-language escape hatch. Arbitrary pointer arithmetic and externally supplied addresses cannot generally be proven safe by the compiler. Code using raw pointers must preserve the storage lifetime and bounds contract. The project must not describe this as complete Rust-style ownership until dynamic bounds/null checks are emitted consistently by all execution backends or unsafe pointer operations receive an explicit language-level boundary.

## Integer safety

Implicit narrowing is rejected by the type checker. `std/math/checked` provides overflow-aware `i64` add/subtract/multiply/divide/modulo/negate/absolute-value operations and saturating helpers. Application code that must not wrap should use these helpers until checked-overflow execution becomes a compiler mode/default.

## Concurrency

Atomic operations default to sequential consistency. Standard read-modify-write helpers use compare/exchange loops rather than load-plus-store sequences. `std/sync` provides a portable spin lock, once state and semaphore built on atomics. `std/task` exposes atomic task-state transitions. `std/thread` is an explicit provider-backed low-level thread lifecycle; application code should prefer typed task abstractions where possible.

## Standard library

The bundled standard library now contains more than one hundred modules. Core maturity work is concentrated in math and big integers, checked arithmetic, random, strings/text/Unicode/UTF-8/bytes/formatting, generic collections, IO/file/filesystem/path/process/environment, time/date, JSON/CSV/XML, HTTP/TCP/UDP/DNS/WebSocket/TLS, SQL/database, concurrency/task/thread, testing/debug/profile, and provider-backed cryptography.

Cryptographic primitives are deliberately not handwritten in `.noqeri`. AES-GCM, SHA-2/HMAC, RSA-OAEP/PSS and Ed25519 APIs validate parameters in Noqeri and delegate primitives to audited platform/runtime providers.

## Tests

`tests/generic_records.noqeri` and `tests/generic_collections.noqeri` cover aggregate generics. `tests/stdlib_maturity.noqeri` exercises pure standard-library behavior, including checked arithmetic, big integers, inverse trigonometry, UTF-8/Unicode, parsing/formatting, JSON/CSV/XML helpers and validation policies. `scripts/test_maturity.sh` also contains negative programs that must be rejected by the compiler for lifetime/null/static-bounds violations.

The normal `scripts/test_core.sh` path invokes the maturity suite after building the compiler.

## Remaining compiler milestones

The following remain language/backend work rather than library work:

1. Emit runtime bounds checks for dynamic array/slice indices in every backend.
2. Emit runtime null checks for nullable raw-pointer dereferences where static proof is unavailable.
3. Introduce an explicit `unsafe` boundary for raw pointer arithmetic, integer-to-pointer conversion and equivalent unchecked operations.
4. Offer checked integer overflow as a compiler execution mode and evaluate making it the default for application profiles.
5. Continue strengthening borrow/lifetime analysis from lexical local-storage tracking toward full aggregate/field/interprocedural lifetime reasoning.
6. Expand provider implementations and cross-platform conformance tests for file/network/thread/crypto extern capabilities.

These are tracked as the safety frontier; they should not be hidden behind standard-library wrappers or marketing claims.
