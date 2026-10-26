# Noqeri syntax and runtime implementation status

**Evidence date:** 2026-10-26. This is an implementation and execution
qualification record, not a product roadmap or a claim of full release
fitness. Every advertised language feature must pass all active compiler
stages and supported backends before being considered generally available.

| Requested work | Verified status | Evidence and limits |
| --- | --- | --- |
| Fix Stage-0 interpreter record/array corruption | **Fixed and ASan-tested** | [Memory CI](https://github.com/EMN90909/Noqeri/actions/runs/40367812816). Record/array address-of now uses aggregate backing storage. |
| Stage-1/Stage-2/Stage-3 reproducible NQRI compiler | **Passed** | [Cross-compiler byte equality](https://github.com/EMN90909/Noqeri/actions/runs/40370068449); trusted Stage-1 pinned with SHA-256 `87575227208576182bcf329ad78340e62e21cda38f74eed63863f3200e40bde2`. This is portable **bytecode** selfhosting through the C++ NQRI VM, not native executable selfhosting. |
| Stage-2 executable parity for `repeat`, array/slice `for-in`, typed expression-bodied functions | **Passed representative execution** | [Stage-2 NQRI execution](https://github.com/EMN90909/Noqeri/actions/runs/40371256165). An extended [three-backend differential run](https://github.com/EMN90909/Noqeri/actions/runs/40381230860) also compares nested loops, break/continue and arrays. Does not imply universal semantic or native-selfhost parity. |
| Private function **parameter and return** type inference | **Partial: Stage-0 return only** | Single unconditional typed private returns infer in Stage-0; parameter inference, complex flow/recursion, exported interfaces and independently verified Stage-2 parity remain outstanding. |
| Allocator-owned growable `List` literals | **Not implemented** | `list_grow_into` now checks caller-supplied replacement capacity and preserves elements, but there is no allocator-owned list, automatic growth or growable literal. [Library regression](https://github.com/EMN90909/Noqeri/actions/runs/40381689774) checks cursor/Unicode behavior and compiles a checked growth wrapper, not allocator ownership. |
| Anonymous record literals with inferred named fields | **Not implemented** | Named records work; structural anonymous layout and ABI remain outstanding. |
| Typed `"Hello {name}"` interpolation | **Not implemented** | Requires compiler-owned typed formatting, escaping, UTF-8 and string runtime. |
| Lambda-based `map` and `filter` | **Not implemented** | Bounded numeric collection helper functions exist, but `=>` lambdas and generic method dispatch do not. |
| `?.`, `??`, typed `Result<T,E>` `?` propagation | **Partial: pointer `??` candidate** | Right-associative, lazy nullable pointer `??` is added in both compiler frontends with type diagnostics and a differential CI gate. `?.`, typed `Result<T,E>`/`Option<T>` unwrapping, and postfix typed `?` propagation remain unimplemented. **Requires CI evidence before qualifying.** |
| One `for-in` protocol for strings, lists, and custom iterators | **Partial** | Array/slice `for-in` passes. `Utf8Iterator` returns Unicode scalars with invalid-input detection, while `Iterator<T>` has list/slice cursor adapters. The compiler does not yet lower `for-in` over Unicode strings, Lists, or user-defined iterators. [Portable regression](https://github.com/EMN90909/Noqeri/actions/runs/40381689774). |
| AI tensor operations callable by ordinary Noqeri source | **Partial, tested** | Native Linux x86-64 AOT calls to GEMM, softmax, SGD, bounded differentiable Tape graphs, ASCII BPE encoding, and single-call transformer training/generation passed. [Native integration CI](https://github.com/EMN90909/Noqeri/actions/runs/40380043631). Explicit linking and caller-owned buffers are required; other backends are not supported. |
| Full autodiff/transformer/tokenizer APIs in ordinary Noqeri | **Partial, tested** | Stateless bounded Tape execution, ASCII/LF BPE and a small transient CPU transformer are callable from Noqeri. Reusable models, every Tape operation, broad Unicode tokenization, persistent training state and NQRI support are not implemented. |
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
