# Noqeri Product Board Review — September 2026

This review treats the compiler, runtime, standard library, website and registry as one product. A language feature is not complete merely because syntax exists, and a package is not mature merely because its source file is large.

## Board roles

- **Planning:** scans the repositories, compares mature language ergonomics, identifies leverage and rejects cosmetic work.
- **Kid tester:** approaches Noqeri like a curious nine-year-old: can the name, first example, failure and mental model be understood without compiler-internals knowledge?
- **Design:** owns syntax, naming, diagnostics, documentation hierarchy and the safe/advanced boundary.
- **Development:** owns compiler semantics, runtime guarantees and library behavior.
- **Testing:** owns positive, negative, safety, portability and performance evidence.
- **Marketing:** may publish only claims supported by a test or benchmark evidence state.

## Repository scan

The library contains a useful middle tier (`math`, `json`, `bytes`, `text`, `utf8`, `base64`, `csv`, `date`, `random`, collections and IO), but many catalog modules began this review as placeholder-shaped files containing little more than `len`, `is_empty`, `first` and `last` helpers under unrelated module names.

Examples included `bitset`, `bloom`, `cache`, `calendar`, `channel`, `clock`, `crc`, `deque`, `duration`, `encoding`, `event`, `future`, `hash`, `heap`, `html`, `ini`, `iterator`, `lexer`, `markdown`, `metrics`, `mime`, `numeric`, `parser`, `pool` and `priority_queue`. `arena`, `ascii`, `backoff` and `binary` had already started growing beyond the original placeholder pattern but still need the same evidence gates.

The board unanimously rejects padding every module to 30 KB. Thirty kilobytes of aliases, duplicated wrappers or comments is not maturity. Broad modules should naturally become substantial as real behavior, tests and examples accumulate; narrow modules can legitimately be smaller when their useful domain is complete.

## Planning research

The planning seat compared the direction with established safety models:

- Rust makes programmer proof obligations visible with explicit `unsafe { ... }` boundaries instead of silently turning ordinary code into unchecked code.
- Swift combines static language guarantees with runtime checking and keeps unsafe pointer facilities explicit for interop/low-level work.
- Noqeri should keep ordinary indexing and application code compact while exposing low-level escape hatches deliberately.

The adopted rule is: **ordinary Noqeri stays compact and checked; operations that bypass ordinary guarantees require an explicit unsafe boundary; NIR represents runtime checks explicitly so optimizers may remove a check only when they can prove it redundant.**

## Kid-tester session

The kid tester tried to answer five questions: “What is this?”, “How do I make one?”, “What is the first useful thing I can do?”, “What happens when I make a mistake?”, and “Can I change the example and predict the result?”

### Friction found

1. **Placeholder modules were misleading.** Four generic helpers made unrelated modules look finished without teaching the abstraction named by the file.
2. **Storage ownership appears too early in low-level containers.** A beginner who asks for a deque first meets backing storage, pointers and capacity instead of `push` and `pop`.
3. **Generic constraints are powerful but unexplained.** `Copy`, `Eq` and `Ord` should be introduced as plain-language capabilities before monomorphisation details.
4. **Boolean failure is simple but can be opaque.** Low-level/freestanding APIs benefit from cheap `bool`/status results; application-level teaching should eventually layer descriptive result values where the reason matters.
5. **Safety modes are hard to discover when they live only in environment variables or advanced docs.** Development-mode recipes should be obvious from CLI help and beginner documentation.
6. **Advanced vocabulary arrives too early.** Borrow, ABI, aggregate and NIR belong after a learner can create a value, use it, and see one safe failure.
7. **Performance wording can outrun evidence.** “Fast” needs a result bundle, not a benchmark filename.

### Kid-tester design rule

Teach features in this order:

`create -> do one useful thing -> read result -> show one safe mistake -> explain representation/advanced controls`

Normal APIs should prefer short verbs such as `push`, `pop`, `set`, `get`, `clear`, `add` and `contains`. Systems controls remain available, but should not be the first mental model a new user must learn.

## Board vote

| Proposal | Planning | Kid tester | Design | Development | Testing | Marketing | Decision |
|---|---:|---:|---:|---:|---:|---:|---|
| Inflate every std file to 30 KB | No | No | No | No | No | No | Rejected |
| Deepen modules by real API coverage | Yes | Yes | Yes | Yes | Yes | Yes | Adopted |
| Runtime bounds checks for dynamic safe indices | Yes | Yes | Yes | Yes | Yes | Yes | Implemented; regression-gated |
| Runtime null checks on checked raw access paths | Yes | Yes | Yes | Yes | Yes | Yes | Implemented; regression-gated |
| Explicit `unsafe {}` around raw pointer operations | Yes | Yes | Yes | Yes | Yes | Yes | Implemented; regression-gated |
| Aggregate/field-sensitive borrow tracking | Yes | Neutral | Yes | Yes | Yes | Yes | Implemented; expanded negative tests added |
| Interprocedural borrow summaries | Yes | Neutral | Yes | Yes | Yes | Yes | Implemented; expanded negative tests added |
| Checked-overflow execution mode | Yes | Yes | Yes | Yes | Yes | Yes | Implemented; regression-gated |
| Publish unmeasured speed claims | No | No | No | No | No | No | Rejected |

## Safety implementation audit

The requested safety work was not starting from zero. The current compiler already contains the mechanisms below, so this tranche strengthens evidence rather than creating duplicate systems.

### Dynamic bounds checks

The NIR/reference execution path contains explicit `CheckBounds` operations. Dynamic fixed-array/slice accesses are checked when they cannot be rejected statically. The safety test suite includes both a valid dynamic index and an out-of-range runtime failure, and inspects emitted NIR for `check_bounds`.

### Runtime null checks

The reference execution path contains explicit `CheckNonNull` operations and negative runtime coverage for null raw-pointer indexing. Compile-time lifetime analysis also rejects provably null dereference/index cases where possible.

### Explicit unsafe blocks

Raw-pointer indexing that bypasses ordinary safe guarantees is rejected outside `unsafe { ... }`. The safety suite contains a negative program without the block and a positive program using the explicit boundary.

`unsafe` is an escape hatch, not a switch that makes the rest of the compiler stop checking. Low-level implementation code should keep unsafe regions as small as practical and expose checked abstractions to normal callers.

### Aggregate and interprocedural borrowing

`Compiler/lifetime.cpp` maintains function summaries for returned/escaping parameter borrows and tracks aggregate field paths. Summary computation iterates to a fixpoint rather than treating every call as opaque.

This tranche adds negative test contracts for:

- an aggregate field retaining a borrow after the borrowed local leaves scope;
- a helper function storing a caller-local borrow through a parameter, requiring interprocedural escape information at the call site.

These tests strengthen the claim, but the project should still avoid saying “complete Rust-equivalent borrow checker” until broader aliasing, mutation, recursion and backend matrices have been demonstrated.

### Checked overflow

The reference interpreter supports `NOQERI_CHECKED_OVERFLOW=1`. In this mode overflowing integer arithmetic traps; default execution remains separately defined. The safety suite contains a negative i64 overflow test under checked mode.

## Foundational standard-library tranche

Five placeholder-shaped modules were replaced with real caller-owned structures on `main`.

### `std/bitset`

Implemented initialization/invariants, indexed test/set/clear/toggle, ranges, counts, first/last/next/previous scans, rank/select, equality/subset/intersection checks, union/intersection/difference/xor/not, shifts, byte import/export, density and Hamming distance.

### `std/bloom`

Implemented configurable caller-owned storage, deterministic byte/u64 hashing, insertion/query, fill/saturation metrics, serialization helpers, union/intersection and sizing/hash-count guidance.

### `std/deque`

Implemented a generic fixed-capacity ring deque with front/back references and values, push/pop at both ends, rotation, reverse/swap, search/count, indexed removal, truncation/drop operations, bulk push/copy and invariant checks.

### `std/heap`

Implemented generic min/max binary heap behavior: push/pop, peek, sifts, heap build, root replacement, push-pop, update/remove, validation, bulk insertion, copy and sorted draining.

### `std/priority_queue`

Implemented a stable generic priority queue using caller-owned value/priority/sequence storage. Equal priorities preserve insertion order; the module supports push/pop, priority update, removal/search, bulk insertion, draining and invariant validation.

Compatibility helpers matching the previous placeholder names remain temporarily so existing source does not break merely because the module became real. They are compatibility surface, not the primary API.

## Test contract for the new structures

`tests/stdlib_structures.noqeri` imports all five public modules and exercises multi-operation behavior rather than copying algorithms into the test fixture. It covers boundaries and invariants such as bitset out-of-range rejection, Bloom add/query/reset behavior, deque ordering/rotation, heap ordering/update, and stable priority-queue ordering.

The repository test command already runs `noqeri test tests`, so the new fixture joins the normal test discovery path.

**Important evidence state:** the fixture is checked in, but this review environment could not obtain a local GitHub checkout because outbound DNS/network cloning was unavailable. Therefore this document records the new suite as a **test contract**, not a passed local result. A release may promote it to `verified` only after executing the suite on that release configuration.

## Performance evidence

`Benchmarks/suite.json` now has explicit evidence states and foundational-structure workload IDs for bitset, Bloom filter, deque, heap and priority queue. `Benchmarks/structures.noqeri` provides a repeated mixed-operation workload for those structures.

Those entries are deliberately marked `unmeasured`. No timing numbers were invented. The existing performance gate already refuses to report a passing comparison when result IDs are not comparable.

Every future measured result should include:

- source commit SHA;
- backend and target;
- compiler/build profile and flags;
- OS/CPU where available;
- safety and overflow modes;
- input size;
- warmup/sample counts;
- raw samples;
- median and p95 (or another documented tail statistic);
- memory/binary-size metrics where relevant;
- exact benchmark fixture and baseline revision.

## Ecosystem/registry gate

The registry quality policy now defines a deep-module contract. Foundational structures must demonstrate real state/algorithms, invariants, normal and failure paths, imported behavior tests, ownership/safety notes, teaching examples and performance fixtures when performance-sensitive.

A module containing only `len`, `is_empty`, `first` and `last` cannot qualify as deep regardless of metadata or file size.

The registry also distinguishes `unmeasured`, `measured-local`, `measured-release` and `regression-gated` performance evidence so package/site metadata cannot silently convert an expectation into a benchmark claim.

## Website evidence policy

The website quality board and machine-readable policy distinguish:

- `planned`;
- `implemented`;
- `test-contract`;
- `verified`;
- `measured`;
- `regression-gated`.

The site should never promote `implemented` or `test-contract` to `verified` merely because source or a fixture exists.

## Remaining standard-library depth queue

This tranche is deliberately not the entire catalog.

**Priority A next:** `binary`, `cache`, `parser`, `lexer`, `encoding`, `numeric`.

**Priority B:** `calendar`, `clock`, `duration`, `event`, `future`, `channel`, `pool`, `iterator`, `metrics`.

**Priority C:** `crc`, `hash`, `html`, `markdown`, `mime`, `ini` and other specialist/domain modules.

Each module exits the queue only with real API depth, imported behavioral tests, documented failure semantics, a beginner-facing normal path, and benchmark/security evidence when its domain demands it.

## Marketing language allowed now

Allowed:

- “Noqeri’s reference execution pipeline contains explicit dynamic bounds and null checks.”
- “Raw-pointer operations require explicit unsafe boundaries in checked source.”
- “The lifetime pass contains aggregate field-path tracking and interprocedural summaries.”
- “The foundational bitset/Bloom/deque/heap/priority-queue modules now have substantive implementations and checked-in behavior tests.”
- “New structure benchmarks are registered as unmeasured until result bundles exist.”

Not allowed without stronger evidence:

- “all backends are memory safe”;
- “full Rust-class borrow safety”;
- “zero-cost checks”;
- “all standard-library modules are mature”;
- “every module exceeds 30 KB”;
- any fixed faster-than percentage versus C/C++/Rust/Zig/Go without a reproducible result bundle.

## Release gate

A safety/performance/library tranche can be promoted from implementation to verified only when:

1. the compiler builds cleanly on a supported host;
2. positive language and imported module tests pass;
3. OOB/null/overflow/unsafe/borrow negative tests fail for the expected reason;
4. native/web backends either preserve the stated guarantee or explicitly report unsupported behavior;
5. touched modules pass focused multi-operation tests;
6. performance-sensitive changes have fixtures, and measured claims have complete environment/raw-sample metadata;
7. website and registry evidence states match the actual results;
8. no source-size or line-count metric is substituted for behavior.
