# Noqeri beginner language and API conventions (October 2026)

This is the **recommended syntax** for new Noqeri source. Explicit low-level
types, pointers, fixed arrays, the old path-import spelling, and existing
library functions remain supported for compatibility.

## 1. Functions: infer private, explicit public

```noqeri
function square(n: int) = n * n
function has_items(count: int) { return count > 0 }

export function calculate_total(price: int, quantity: int): int {
    return price * quantity
}
```

The Stage-0 compiler infers a private, nongeneric function's return type when
the body has a **single unconditional** return expression, all parameters
are annotated and the expression has a resolvable nonvoid type. Expression
bodies are shorthand for that return. Explicit `: Type` continues to work.
A private function with no return values continues to default to `void`.

A function with an ambiguous body, an unresolved or recursive return call,
untyped parameters, or a public/generic single-return ABI needs an explicit
annotation. The compiler rejects ambiguous inference and points to the
function name. This is not dynamic typing, flow-sensitive multi-branch
inference or cross-stage self-hosted parity.

## 2. Collections: write the simple thing first

For an ordinary known-size collection, use a literal:

```noqeri
let scores = [10, 20, 30]
print(len(scores))
print(scores[1])
```

The compiler infers the element type and array length. Beginners do not
have to write `[i64; 3]`, `usize`, or pointer casts just to make an array.
Use `slice(scores)` with `std/collections` utilities where useful.

For mutation with caller-owned storage, the existing bounded
`List<T>`, `Map<K,V>` and `Set<T>` APIs now share:

| Container | Count | Membership | Add/update |
| --- | --- | --- | --- |
| List | `list_size` | `list_has` | `list_add` |
| Map | `map_size` | `map_has` | `map_add` |
| Set | `set_size` | `set_has` | `set_add` |

Adding to an already full bounded collection returns `false`. The generic
record implementations currently need caller-provided backing storage,
so they are **not yet** Python-style growable collections. Do not describe
ordinary `[...]` arrays as automatically growable. Ownership, allocation,
`List` literals, map/set literals and lifetime-safe append require a future
language/runtime implementation.

## 3. One normal import spelling

Use **quoted module paths** everywhere:

```noqeri
import "std/list"
import "std/json"
import "std/result"
import "ai/native"
```

For a local file:

```noqeri
import "./helpers.noqeri"
```

For the Noqeri web UI, where namespaces are required:

```noqeri
import "noqeri/ui" as ui
```

The native web renderer and separate browser view compiler accept the quoted
UI import. The previous unquoted `import noqeri/ui as ui` is still read for
legacy projects. A standard-source import resolves under
`NOQERI_STDLIB_ROOT` (the path to the installed `Lib` directory) or the
nearest enclosing checkout's `Lib` directory; no network lookup or package
installation is implicit. Relative local imports continue to work.
The current Stage-0 module graph merges imported module symbols into one
namespace; it **does not** yet provide full package-qualified namespace
resolution for ordinary compiled modules.

## 4. Useful errors that say what to fix

```text
error[NQR-T3002]: unknown symbol 'absent'
 --> main.noqeri:1:14
 1 | let value = absent
   |             ^^^^^^
 help: Declare this name with let or const before using it, or check the spelling.
```

`noqeri check main.noqeri --json` exposes a schema-versioned error array
with filename, byte offsets, line, column, code, message and `help`.
Common parser, import, collection, symbol, type and inference errors have
repair suggestions; an unknown diagnostic code has a generic fallback.
The native compiler uses one-based source line/column and zero-based byte
offsets. Unicode-grapheme visual alignment is not fully implemented.

## 5. One success/failure convention

Prefer a typed `Result<T>` or `Outcome<T>` from `std/result` when
composing library operations. For today’s existing native host ABIs, use:

- **Status-producing operations** (files, filesystem, HTTP request handles,
  JSON token counts and native AI): nonnegative result means success;
  negative means failure. `status_ok(status)` and
  `status_error(status)` are common checks in `std/result`.
- **HTTP response status**: `http_client_response_ok(code)` means a
  response code from 200 through 299, and differs from a successful request
  handle (`http_client_request_ok(handle)`).
- **NoeSQL**: `database_ok(db)` checks that the connection is open;
  transaction methods currently return booleans and
  `database_last_error(db)` provides the native provider error string.
- **Collections**: `*_add` returns a boolean capacity/success result and
  `*_has` reports membership.

Important: some NoeSQL operations currently return empty strings on a closed
handle, rather than a typed error. They have **not** been converted to
`Result<T, E>` or reliable typed success/error propagation. The external
native AI SDK’s full autodiff and transformer training APIs are not yet
callable as normal Noqeri source imports. Do not conceal these limitations
behind wrappers.

## Verification and remaining gates

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target noqeri
python3 tests/beginner_syntax.py ./build/noqeri .
node --test tests/beginner_view_imports.test.mjs
```

The regression suite checks valid and invalid inference, original
file/line/column diagnostic spans, nonempty repair suggestions, standard
import paths, collection literals and quoted browser UI imports.

This change is a Stage-0 and framework-subset increment, **not** complete
Stage-2 parity, growable collection ownership, typed `?` propagation or
production web/NoeSQL/AI ABI unification.
