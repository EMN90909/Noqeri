# Experimental nullable pointer `??` (Stage-0 / Stage-2)

The compiler accepts `left ?? right` only when one or both sides are the
same pointer type (or the literal `null`). The left operand is evaluated
once. The right is evaluated only when the left is null. Expressions associate
right-to-left and bind less tightly than boolean `||` but more tightly than
assignment. Return type is the pointer type, never a nullable wrapper.

```noqeri
let fallback: int = 7
let missing: *int = null as *int
let chosen: *int = missing ?? &fallback
```

This deliberately **does not** add option/result unwrapping, `?.`, or postfix
`?` propagation. Borrow/lifetime checking still applies to pointer values.
Tests: `stage2_pointer_coalesce.noqeri`, and invalid numeric/mismatched-pointer
fixtures. The cross-runtime gate checks Stage-0, rebuilt Stage-2 NQRI and
Linux x86-64 native AOT; do not mark generally supported until CI is green.
