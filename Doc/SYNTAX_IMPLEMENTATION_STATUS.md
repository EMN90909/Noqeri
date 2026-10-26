# Noqeri syntax and runtime implementation status

**Evidence date:** 2026-10-26. This is an implementation and execution
qualification record, not a product roadmap or a claim of full release
fitness. Every advertised language feature must pass all active compiler
stages and supported backends before being considered generally available.

| Requested work | Verified status | Evidence and limits |
| --- | --- | --- |
| Fix Stage-0 interpreter record/array corruption | **Fixed and ASan-tested** | [Memory CI](https://github.com/EMN90909/Noqeri/actions/runs/40367812816). Record/array address-of now uses aggregate backing storage. |
| Stage-1/Stage-2/Stage-3 reproducible NQRI compiler | **Passed** | [Cross-compiler byte equality](https://github.com/EMN90909/Noqeri/actions/runs/40370068449); trusted Stage-1 pinned with SHA-256 `87575227208576182bcf329ad78340e62e21cda38f74eed63863f3200e40bde2`. This is portable **bytecode** selfhosting through the C++ NQRI VM, not native executable selfhosting. |
| Stage-2 executable parity for `repeat`, array/slice `for-in`, typed expression-bodied functions | **Passed representative execution** | [Stage-2 NQRI execution](https://github.com/EMN90909/Noqeri/actions/runs/40371256165). Does not imply parity for every future syntax feature. |
| Private function **parameter and return** type inference | **Not implemented** | Typed expression bodies exist; omitted parameter/return inference is not yet deterministic/sound across call sites, recursion and exports. |
| Allocator-owned growable `List` literals | **Not implemented** | Current `List<T>` uses caller-owned buffers. Fixed arrays and typed slices work. |
| Anonymous record literals with inferred named fields | **Not implemented** | Named records work; structural anonymous layout and ABI remain outstanding. |
| Typed `"Hello {name}"` interpolation | **Not implemented** | Requires compiler-owned typed formatting, escaping, UTF-8 and string runtime. |
| Lambda-based `map` and `filter` | **Not implemented** | Bounded numeric collection helper functions exist, but `=>` lambdas and generic method dispatch do not. |
| `?.`, `??`, typed `Result<T,E>` `?` propagation | **Not implemented** | The library has `Option<T>`/`Result<T>` data structures and status-based `try`/throw, not the requested operators or typed propagation. |
| One `for-in` protocol for strings, lists, and custom iterators | **Partial** | Array/slice `for-in` passes. The complete Unicode/lifetime-aware protocol is not defined or lowered. |
| AI tensor operations callable by ordinary Noqeri source | **Partial, tested** | Native Linux x86-64 AOT calls to GEMM, softmax and SGD have passed. [Native integration tests](https://github.com/EMN90909/Noqeri/actions/runs/40373362461). The C++ SDK implements more than this exposed language ABI. |
| Full autodiff/transformer/tokenizer APIs in ordinary Noqeri | **Not implemented** | Complete C++ `Tape`, BPE and transformer APIs remain SDK/CLI features. |
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
