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

The Stage-2 implementation type-checks through Stage-0, but its *runtime
parity is not yet qualified*: the initial execution of the self-hosted
AST/NIR regression under the Stage-0 reference interpreter crashed with
heap corruption. Consequently no Stage-2 execution claim is made.
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
bounds-checked NIR lowering; its actual runtime parity remains unqualified.
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
- `for item in items` now lowers arrays and slices in both frontends. The iterable is evaluated once; `continue` advances; the Stage-2 NIR path guards bounds. Strings, growable lists and custom iterator objects are **not yet supported** by this syntax and require a common iterator protocol.
- Growable `List` literals and anonymous record literals: lifetime,
  allocation, structural field identity and ABI need design and tests.
- `"Hello {name}"`: requires typed string formatting, escaping and safe
  conversion across bootstrap, native and Stage-2 backends.
- `readText(path)?`: file capabilities need a bounded, platform-independent
  adapter and typed propagation, not an unchecked `string` allocation.
- `?.`, `??`, collection lambdas, high-order `map`/`filter`, and native
  Noqeri AI framework bindings: specification and parity remain pending.

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
