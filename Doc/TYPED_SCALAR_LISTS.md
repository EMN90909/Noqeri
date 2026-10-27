# Typed growable scalar lists (implementation increment)

Status: **experimental; restricted scalar element types**.

This change extends contextual growable list promotion beyond `List<int>` to
`List<float>` and `List<bool>`. It does **not** implement an allocator-owned
generic `List<T>` for strings, records, pointers, or custom destructor-bearing
types.

## Syntax

```noqeri
let readings = [1.5, 2.5]
readings.append(3.5)
let latest = readings.pop() // float
let sum: float = 0.0
for reading in readings {
    sum = sum + reading
}
```

A mutable, local, unannotated array literal is promoted to an owned
growable list when a **later direct `.append(...)` call refers to that binding**,
either in the same block or inside a nested `if`, `while` or `repeat` body.
The compiler respects local shadowing and ignores appends occurring before
the declaration. Without that evidence, ordinary `[...]` literals remain
fixed arrays. The literal's element type is retained by the checker,
the internal NIR, and the owned-list runtime (no implicit integer conversion).
Mixed-element encodings are rejected rather than reinterpreted, e.g.
`[1.5, 2]` followed by `.append(3.5)` is invalid. Use `[1.5, 2.0]`.

The supported methods remain `.append(value)`, `.length()`, `.pop()`, and
`.clear()`, plus bounds-checked indexing and indexed assignment. A growable
list cannot be returned, copied, or aliased. For an empty `[]` list, the first direct scalar literal append
supplies the element type (`int`, `float` or `bool`), including inside
nested blocks. If there is no scalar literal evidence, the existing
`List<int>` fallback remains; an incompatible later append is rejected.
This is bounded literal inference, not fully general type-directed
collection inference.

## Ownership and execution

For this restricted scalar subset, list handles are dynamically allocated
with the existing one-million-element cap and released by existing lexical
cleanup lowering on supported normal and early exits. Generic payload
clone/move/drop, escaping/returning owned collections, captured closures,
provider errors, and exception-style cleanup across arbitrary host failures
remain outside this change. Handles are not a stable external ABI.

## Qualification

The GitHub Actions workflow
`.github/workflows/beginner-growable-lists.yml` now runs
`tests/beginner_growable_scalar_lists.noqeri` against the Stage-0
interpreter, C++ NQRI runtime, JavaScript NQRI reference runtime,
self-hosted Stage-2 compiler NQRI output, and Linux x86-64 native AOT.
`tests/beginner_growable_bad_mixed_float.noqeri` must be rejected by
both source checkers. These gates validate representative behavior,
**not** every backend and supported target.

Do not describe this as complete generic collection support,
string interpolation, lambdas/`map`/`filter`, `Result<T,E>?`,
or unified string/map/set/custom iterator support.

## Lexical inference regression (2026-10-27)

```noqeri
let readings = []
if true { readings.append(1.5) }
readings.append(2.5)

let flags = []
repeat 2 { flags.append(true) }
```

The test fixtures `tests/beginner_growable_lexical_inference.noqeri`
and `tests/beginner_growable_bad_empty_inference.noqeri` are included in
`beginner-growable-lists.yml` for Stage-0, NQRI, rebuilt Stage-2 and
Linux x86-64 native AOT qualification. CI results, not the existence of
the tests, determine which of those targets is verified.

String- and record-element lists, anonymous records, lambdas, interpolated
strings, unconstrained iterator dispatch, escaping ownership and generic
`Result<T,E>?` remain outside this increment.
