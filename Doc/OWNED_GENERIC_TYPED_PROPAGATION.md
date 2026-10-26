# Owned generic collections and typed postfix propagation — qualification

Status: **experimental, Stage-0/native increment** (October 2026). Do not
confuse a parser feature or a host library symbol with an end-to-end supported
language capability. Merging is gated on the dedicated native and parser tests.

## Type-driven hosted collections

The native runtime accepts a size, alignment and optional clone/drop callback
contract. It aligns element storage, resizes geometrically, validates opaque
handles, bounds memory/elements, guards concurrent operations, and rolls back
mutations on allocation or clone failure.

```noqeri
import "std/owned_generic"

let numbers: OwnedList<int>
let ready = owned_generic_list_open(0, &numbers)
if ready > 0 {
    owned_generic_list_append(&numbers, 5)
    let found: Outcome<int>
    owned_generic_list_get(&numbers, 0 as isize, -1, &found)
    owned_generic_list_close(&numbers)
}
```

A `Copy`-constrained, specialized `OwnedList<T>` supports scalar `T`.
The compiler derives `sizeOf(sample)` and `alignOf(sample)` *before*
native lowering. Values are copied into storage, not pointers to temporary
stack locations. `OwnedMap<K,V>` uses integer keys `K: Integer` and
`V: Copy`; `OwnedSet<T>` uses integer keys `T: Integer`. These
properties avoid unsafe bitwise equality on arbitrary records and borrowed
strings. Operations are explicit, and a successful `close` releases storage.

The C/C++ ABI additionally accepts a **clone/drop pair** for nontrivial list
elements; it clones on growth/removal, destroys replaced storage and rolls back
on clone failure. Callbacks must not throw or reenter the host runtime. Any
clone returned by `get` is independently owned by the caller and must be
dropped. No equivalent callback registration or destructor codegen exists in
normal Noqeri source yet, so this is **not arbitrary `List<T>` ownership
parity**, nor a complete `move`/destructor language feature.

Other constraints: hosted native (not portable NQRI/freestanding), explicit
close (no automatic deterministic destructor), opaque handle lifetimes are
validated at runtime, type-erased map/set storage uses Copy semantics only,
and aliases of live list storage are rejected.

## Typed `?`: a deliberately bounded lowering path

```noqeri
import "std/result"

function validate(input: *Result<int>): *Result<int> {
    let value = input?
    // On an error, the original Result pointer was returned already.
    return input
}
```

The Stage-0 parser uses a dedicated postfix token rather than conflating
`?` with `??` or the existing integer `try` instruction. Its checker
requires a borrowed pointer to `Result<T>` or `Outcome<T>`, a scalar
integer/bool/pointer `value`, a bool `ok` field, and a matching function
return pointer type. Lowering tests `ok` and either extracts the typed
value or returns the original error-bearing pointer. This preserves error
code, domain and message without constructing a new record.

**Not yet complete**: direct returned-by-value `Result<T,E>` ABI,
different successful payload types across propagation, automatic native API
wrapping, aggregate/float payload propagation and self-hosted Stage-2 parity.
The current Stage-2 compiler's postfix `?` means legacy integer status
propagation, not the new typed operation; its grammar/semantic/NIR paths must
be updated before claiming parity. The existing native APIs may still take
output pointers. Noqeri applications should not use this feature expecting
full Rust-style `Result` semantics.

## Regression gates

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target noqeri noqeri_owned_collections noqeri-owned-value-list-test
ctest --test-dir build -R '^noqeri-owned-value-list$' --output-on-failure
./build/noqeri check tests/owned_generic_native.noqeri --unsafe
node scripts/native-build.mjs tests/owned_generic_native.noqeri build/owned-generic \
  --compiler=build/noqeri --owned-collections --owned-collections-lib=build/libnoqeri_owned_collections.a
./build/owned-generic
./build/noqeri run tests/layout_inference.noqeri
./build/noqeri run tests/typed_question_propagation.noqeri --unsafe
```

An executable pass of these commands qualifies the native subset only.
