# Everyday syntax and Stage-2 qualification

Noqeri remains a general-purpose, statically checked language with explicit
low-level facilities for systems code. Shorter notation must preserve those
guarantees in the Stage-0 bootstrap and Stage-2 self-hosted compilers.

## Counted repetition: Stage-2 implementation

```noqeri
repeat 4 { print("hello") }
```

The Stage-2 source implementation parses counted repetition into a dedicated
`nqAstRepeat()` AST node. Semantic analysis requires a numeric count.
The NIR lowering evaluates the count once, converts it to `int`, skips
zero/negative counts, decrements **before** the body, and ensures `continue`
advances without locking up. Nested loops have independent loop-head and
break-chain state. The bootstrap compiler already supports `repeat`.

The Stage-2 parser and bounded AST/NIR tests now execute under the Stage-0
reference interpreter without heap corruption. AddressSanitizer identified an
incorrect aggregate address calculation (record addresses were taken from the
host's eight-byte scalar storage cell). The fix makes aggregate addresses refer
to their allocated backing storage and ASan passes the six bounded tests.
This establishes **bounded parser/lowering execution** only; it does not prove
a full Stage-2 NQRI bootstrap, all semantic/backend compatibility, or native
self-hosting. Subsequent [reproducible bytecode bootstrap](https://github.com/EMN90909/Noqeri/actions/runs/40370068449) and [actual Stage-2 NQRI execution](https://github.com/EMN90909/Noqeri/actions/runs/40371256165) now verify the NQRI path for the three covered syntax forms; native selfhosting is still unqualified. See [issue #115](https://github.com/EMN90909/Noqeri/issues/115).
The Stage-2 bootstrap/rebuild proof certificate remains fail-closed.

## Simple iteration

```noqeri
let prices = [100, 250, 80, 400]
for price in prices {
    print(price)
}
```

Stage-0 desugars this into a lexical block with one iterable binding,
index and a while loop. Stage-2 has a dedicated AST node and intended
bounds-checked NIR lowering; its actual runtime parity for this limited array/slice case now passes the [Stage-2 NQRI execution gate](https://github.com/EMN90909/Noqeri/actions/runs/40371256165).
For now the supported iterables are typed fixed arrays and slices.

## Collections: first ergonomic, nonallocating operations

```noqeri
import "../../Lib/std/numeric.noqeri"

let prices = [100, 250, 80, 400]
print(sum(slice(prices)))
```

The `sum` API takes a typed integer slice; `slice(prices)` is currently
required for fixed-array inputs. For overflow-sensitive work, use
`sum_checked(slice(prices), &total)` returning a boolean without writing
partial results. Default integer `sum` uses the compiler's normal overflow
semantics; do not use it for untrusted values without an overflow policy.
Also available:

- `average_or(values, fallback)` computes a floating-point mean.
- `count_at_least(values, threshold)` counts numeric values meeting a bound.
- `filter_at_least(values, threshold, destination)` writes a filtered
  prefix to caller-owned storage, returning its length or `-1` on insufficient
  capacity without partially modifying the destination.
- `map_add(values, amount, destination)` performs elementwise addition into
  caller-owned storage.

These APIs are standard-library functions, not dedicated AI syntax. They
run without dynamic allocations and can be used with the existing
`List<T>` backing buffer abstractions.

## Honest status of other proposals

The following remain **design targets, not implemented syntax**:

- `function add(a,b) = a+b`: sound cross-call inference requires compiler-wide type inference, ABI and Stage-2 work. Typed expression bodies, such as `function add(a: int, b: int): int = a + b`, are supported in both parsers.
- `for item in items` lowers arrays, slices, and contextually growable `List<int>` in both frontends. The iterable is evaluated once; `continue` advances; bounds checks apply. Strings, `List<T>` for arbitrary element types and custom iterator objects are **not yet supported** by this syntax.
- Growable `List` literals and anonymous record literals: lifetime,
  allocation, structural field identity and ABI need design and tests.
- `"Hello {name}"`: requires typed string formatting, escaping and safe
  conversion across bootstrap, native and Stage-2 backends.
- `readText(path)?`: file capabilities need a bounded, platform-independent
  adapter and typed propagation, not an unchecked `string` allocation.
- `?.`, `??`, collection lambdas, high-order `map`/`filter`, and full native
  Noqeri AI framework bindings: specification and parity remain pending. GEMM, softmax and SGD have separate [verified native-AOT entry points](https://github.com/EMN90909/Noqeri/actions/runs/40373362461).

Canonical imports remain `import "./relative.noqeri"` and
`import package "namespace/name"`; versions belong in the NQON project
manifest and lockfile. Existing typed `Result<T>` and `Outcome<T>`
remain the nonexception error pathways.

The goal is fewer characters **without** hidden dynamic typing, implicit
network access, or unbounded memory. No empirical Python/Lua simplicity
ranking is claimed.

## Bounded file convenience

`Lib/std/file.noqeri` now provides `readText(path, destination)` and
`writeText(path, bytes)`: the caller owns buffers, and both operations return
byte counts or negative statuses. `readText` detects and rejects a file
that does not fit in the supplied buffer. This is not the proposed
`readText(path)?` string-and-typed-result API: automatic allocation, UTF-8
decoding and `?` propagation are not yet implemented. The functions
require supported file-host ABI services; the CI currently type-checks the
API rather than exercising real filesystem integration.

For the evidence-backed complete feature matrix, see [Syntax and runtime implementation status](SYNTAX_IMPLEMENTATION_STATUS.md).

## Explicit List growth and UTF-8 iteration

`list_init` and `list_grow_into` provide a checked way to rebind an
existing `List<T>` to a larger **caller-supplied buffer** without losing
elements. No implicit allocation or hidden ownership is introduced.
`iterator_from_slice` and `iterator_from_list` use the existing
`Iterator<T>` cursor protocol; borrowed iterators must not outlive a
buffer rebind. `Utf8Iterator` reads validated **Unicode scalar values**,
not raw UTF-8 bytes, and reports end-of-input separately from malformed
sequences. The regression is `tests/collection_growth_unicode.noqeri`.

These additions now have a qualified **`List<int>` subset** with contextual
owned literals, automatic growth, safe indexed reads and direct `for-in`.
Generic `List<T>` and automatic custom iterator
dispatch in the compiler.

## Private parameters constrained by literal calls (experimental)

A private non-generic function may omit parameter annotations when every
omitted parameter has at least one direct scalar-literal call site, and all
such call sites agree exactly (int, bool, float, string). For example,
`function add(a,b) = a+b` with `add(19,23)` has an unambiguous `int`
argument pair. Conflicting `add(1,2)` / `add(true,false)` calls or no
literal evidence are hard compile-time errors with suggested annotations.
This is deliberately not general Hindley–Milner inference, recursion,
flow-sensitive inference from variable expressions, inferred public APIs,
or dynamically typed parameters.

## Postfix status propagation

`value?` is being introduced as the more concise spelling of
`try value`, **only** for integer status expressions inside functions
with integer return types. It propagates a negative status early and
otherwise evaluates to the original value. Boolean, string and
noninteger expressions are rejected. The syntax is deliberately **not**
`Option<T>`/`Result<T,E>` propagation and is subject to executable
Stage-0 / Stage-2 / native AOT qualification.

## Differential execution qualification (2026-10-26)

The [20-case deterministic property corpus](https://github.com/EMN90909/Noqeri/actions/runs/40401405838)
passed in Stage-0 and rebuilt Stage-2 NQRI. Three varied fixtures were
also compiled and run as Linux x86-64 native executables. The generator
computes expected results independently in Python and tests signed arithmetic,
array iterations, negative/zero/positive repeat counts, nested breaks and
continues and branches. This is bounded evidence, **not** a mathematical
proof of all Stage-2 semantics, native selfhosting, or unimplemented features.

The [postfix status propagation gate](https://github.com/EMN90909/Noqeri/actions/runs/40401036299)
passed across the same three execution paths for integer-status `?`.
Full typed `Result<T,E>` and optional chaining `?.` are still outstanding.

## Beginner-owned integer lists (qualified subset)

```noqeri
let numbers = [5, 10]
numbers.append(15)
print(numbers[2])        // 15

let empty = []
empty.append(31)

for number in numbers {
    print(number)
}
```

No preallocated buffers, imports, or explicit native linker flags are needed
on the qualified Linux x86-64 AOT path. A mutable, unannotated local
literal can become a growable scalar list (`int`, `float`, `bool`) when it
has a later direct `.append()` in the same or a nested lexical block. Empty
literals infer scalar element types from literal append evidence, defaulting
to `int` otherwise. Explicit fixed arrays retain fixed-length semantics.
Lists cannot currently be copied, returned by value, or borrowed by address;
bounds-checked indexed assignments are supported in the scalar subset. The compiler closes them on ordinary lexical exits
and tested integer-status early propagation; it is **not** a fully general
owned/generic collection with guaranteed cleanup on every exceptional exit.
[Stage-0/Stage-2/NQRI/native differential gate](https://github.com/EMN90909/Noqeri/actions/runs/40413683668).

## Owned integer-list indexed mutation (experimental)

A contextually inferred owned `List<int>` supports `numbers[index] = value`
with the same checked index bounds as `numbers[index]` reads. The runtime
updates the element in place, without exposing a pointer that could become
invalid when appending causes reallocation. Other element types, borrowing
`&numbers[index]`, and safe copying or returning a list are not supported.

## Owned list stack methods

The contextually inferred `List<int>` also supports `.pop()` (returns
and removes the last integer) and `.clear()` (releases all elements but
retains the list handle). Popping an empty list is a checked runtime error,
not a sentinel value. Full generic `List<T>`, recoverable typed
out-of-bounds errors and guaranteed cleanup on every exceptional exit remain
future work.
