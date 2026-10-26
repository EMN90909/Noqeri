# Noqeri syntax and runtime implementation status

**Evidence date:** 2026-10-26. This is an implementation and execution
qualification record, not a product roadmap or a claim of full release
fitness. Every advertised language feature must pass all active compiler
stages and supported backends before being considered generally available.

| Requested work | Verified status | Evidence and limits |
| --- | --- | --- |
| Fix Stage-0 interpreter record/array corruption | **Fixed and ASan-tested** | [Memory CI](https://github.com/EMN90909/Noqeri/actions/runs/40367812816). Record/array address-of now uses aggregate backing storage. |
| Stage-1/Stage-2/Stage-3 reproducible NQRI compiler | **Passed** | [Cross-compiler byte equality](https://github.com/EMN90909/Noqeri/actions/runs/40370068449); trusted Stage-1 pinned with SHA-256 `87575227208576182bcf329ad78340e62e21cda38f74eed63863f3200e40bde2`. This is portable **bytecode** selfhosting through the C++ NQRI VM, not native executable selfhosting. |
| Stage-2 executable parity for `repeat`, array/slice `for-in`, typed expression-bodied functions | **Passed representative execution** | [Stage-2 NQRI execution](https://github.com/EMN90909/Noqeri/actions/runs/40371256165). An extended [three-backend differential run](https://github.com/EMN90909/Noqeri/actions/runs/40381230860) compares nested loops, break/continue and arrays; a [seeded 20-program corpus](https://github.com/EMN90909/Noqeri/actions/runs/40401405838) adds varied arithmetic, arrays, conditional flow and nested loops across Stage-0 and rebuilt Stage-2, with three native-AOT samples. Does not imply universal semantic or native-selfhost parity. |
| Private function **parameter and return** type inference | **Partial: qualified scalar literal-callsite inference** | Private unannotated parameters with consistent direct `int`, `float`, `bool` or `string` literal evidence infer exact static types in both compilers; Stage-0, rebuilt Stage-2 NQRI and native AOT agree. Negative tests reject conflicting, missing, and public evidence. [Differential CI](https://github.com/EMN90909/Noqeri/actions/runs/40400201218). General contextual/recursive inference, closures and generic type-parameter inference remain unimplemented. |
| Allocator-owned growable `List` literals | **Partial: verified beginner `List<int>`** | Direct local `let numbers = [5, 10]; numbers.append(15)` and context-driven empty `[]` support growable storage, `.length()`, bounds-checked `numbers[index]` reads and indexed assignment, `.pop()`/`.clear()` methods, and `for item in numbers`. Verified in Stage-0, C++-emitted NQRI, self-hosted Stage-2-emitted NQRI and Linux native AOT. [Four-path qualification](https://github.com/EMN90909/Noqeri/actions/runs/40413683668). Generic `List<T>`, default growability without contextual append, generic element ownership, escaping ownership and fully generalized all-exit cleanup remain incomplete. |
| Anonymous record literals with inferred named fields | **Not implemented** | Named records work; structural anonymous layout and ABI remain outstanding. |
| Typed `"Hello {name}"` interpolation | **Not implemented** | Requires compiler-owned typed formatting, escaping, UTF-8 and string runtime. |
| Lambda-based `map` and `filter` | **Not implemented** | Bounded numeric collection helper functions exist, but `=>` lambdas and generic method dispatch do not. |
| `?.`, `??`, typed `Result<T,E>` `?` propagation | **Partial: pointer `??`, verified integer-status postfix `?`, typed native Outcomes** | Pointer `??` passes Stage-0/Stage-2/native differential tests. `std/native_outcome` wraps file/fs, HTTP, JSON, NoeSQL transaction and native AI services in `Outcome<T>` with error code/domain/message and explicit cross-type propagation; aggregate string-field runtime tested. Postfix `?` now executes as typed integer-status `try` in Stage-0, rebuilt Stage-2 NQRI and native AOT; negative bool/global tests reject invalid propagation. [Postfix CI](https://github.com/EMN90909/Noqeri/actions/runs/40401036299). Typed `Result<T,E>` postfix propagation, `?.` and universal provider ABI/error parity remain incomplete. [Qualification](Doc/STAGE2_OWNED_RESULTS.md). |
| One `for-in` protocol for strings, lists, and custom iterators | **Partial** | Array/slice `for-in` passes. `Utf8Iterator` returns Unicode scalars with invalid-input detection, while `Iterator<T>` has list/slice cursor adapters. The compiler now lowers `for-in` over **`List<int>`** as well as arrays/slices in the tested backends; Unicode strings and arbitrary user-defined iterators remain unsupported. [Portable regression](https://github.com/EMN90909/Noqeri/actions/runs/40381689774). |
| AI tensor operations callable by ordinary Noqeri source | **Partial, tested** | Native Linux x86-64 AOT now qualifies GEMM, softmax, SGD, bounded Tape graphs, ASCII BPE, one-shot training/generation, and explicitly managed **reusable** CPU models (opaque tokens and stale-handle rejection). [Native model integration CI](https://github.com/EMN90909/Noqeri/actions/runs/40399043109). Requires explicit native linking and is not NQRI portable. |
| Full autodiff/transformer/tokenizer APIs in ordinary Noqeri | **Partial: reusable native models qualified** | Opaque reusable `u64` model handles now support train, repeated generate and explicit release, plus bounded Tape, ASCII BPE and SGD/softmax/GEMM through the native AOT ABI. [Native AI CI](https://github.com/EMN90909/Noqeri/actions/runs/40399043109). Persistent model serialization, all Tape operations, Unicode tokenization, cross-platform NQRI host services and production training remain outstanding. |
| Native x86-64 float arithmetic | **Partial, tested** | SSE2 add/sub/mul/div, comparisons and negation pass [AOT CI](https://github.com/EMN90909/Noqeri/actions/runs/40372482610); general float printing, all casts, all architectures and all math functions are not qualified. |
| Native-code self-hosting | **Not yet qualified** | Stage-2/Stage-3 *NQRI bytecode* is reproducible; no full Noqeri-written ELF/COFF/Mach-O compiler code emission and native stage rebuild proof. |

## Required gate for remaining requested syntax

Each feature must demonstrate lexer/AST/type-checker, lowering, runtime,
negative diagnostics, Stage-0 / rebuilt Stage-2 behavior, sanitizers and
representative platform tests. Unsafe or ambiguous inference must be rejected,
never quietly changed to dynamic typing or undefined behavior.

Outstanding implementation and acceptance tasks are tracked in
[issue #116](https://github.com/EMN90909/Noqeri/issues/116).
The original Stage-2 corruption blocker [#115](https://github.com/EMN90909/Noqeri/issues/115)
was closed with sanitizer, reproducible NQRI rebuild and executable Stage-2
evidence. Claims about *native* selfhosting remain explicitly out of scope
of that closure.

## Contextually growable integer lists (native/compiler candidate)

The compiler recognizes an *unannotated* local integer array initializer
which is subsequently mutated with a direct same-scope `.append(value)`
expression statement, promoting it to an owned growable `List<int>`.
Fixed arrays with explicit `[int;N]` annotations remain unchanged.
Initial elements and appended values must be `int`; `.length()`
returns the live length. NIR emits automatic close at normal lexical-scope
exit and before explicit returns. Native Linux x86-64 AOT links the runtime
without a special flag, and both the reference interpreter and NQRI VM
recognize list operations. The runtime traps rather than silently ignoring
allocation and capacity errors. Do **not** assume automatic close on every
exceptional/control-flow escape, or support for aliases, generic types,
indexed **reads** and list `for-in` now have four-path execution evidence;
mutable indexing, nested append expressions, general `List<T>` and
full exception-safe ownership remain unimplemented.
[Executable qualification](https://github.com/EMN90909/Noqeri/actions/runs/40413683668).

## Owned list cleanup on early returns

The compiler now uses the allocation's SSA handle to close an owned
`List<int>`, rather than reloading a local variable by name. This protects
cleanup from local shadowing and reassignment hazards. Integer-status
postfix `?` expands to an explicit negative-status branch while a list
is live: it releases the owned allocations before returning the failure.
Stage-2 uses the same NIR branch/close/return sequence. Validation is
tracked by `tests/beginner_growable_try_cleanup.noqeri`; the feature is
not qualified until the native/reference/selfhost CI is green.

## Cross-runtime beginner list acceptance

The exact beginner program `let numbers = [5, 10]; numbers.append(15)`
now executes without imports or buffer mechanics. After bounded semantic
and lowering fixes, [CI 40411023807](https://github.com/EMN90909/Noqeri/actions/runs/40411023807)
passed in the Stage-0 interpreter, C++-emitted NQRI, *self-hosted Stage-2-
emitted* NQRI and Linux native AOT. The Stage-2 implementation uses eight
checked scalar ownership slots to avoid the VM's embedded-record-array bug.
The interpreter and native host limit a collection to 1,000,000 items.
The compiler preserves explicit fixed-array behavior and rejects copying
or returning an implicitly owned list. Handling negative integer-status
`?` while lists are live is validated separately; no claim of complete
exception safety or unconstrained generic ownership is made.
