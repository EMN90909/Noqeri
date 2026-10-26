# Stage-2 inference, owned collections and typed native outcomes

Implementation increment: **26 October 2026**. Compiler and runtime features have
separate qualification gates. Passing a test for one target does not imply an
equivalent implementation on every platform.

## Self-hosted inference

The Noqeri-written Stage-2 parser and semantic pass implement the same
*deliberately bounded* inference rule used by Stage-0:

```noqeri
function twice(value: int) = value * 2
function ready(value: int) { return value > 0 }
```

A private nongeneric function with **annotated parameters** and a **single
unconditional return expression** may omit its return type. The inferred type
must be statically known. Recursive or unresolved values fail; exported,
external and generic public ABI contracts still require explicit type
annotations. Complicated branch/loop flow and parameter inference are not
automatically inferred.

The Stage-2 parser no longer rejects an unannotated expression-bodied
function solely for its missing return annotation. Instead, semantic analysis
enforces the unambiguous rule, supplies the resolved result to downstream
function calls, and rejects bad signatures. The execution fixture is
`tests/inferred_return_executable.noqeri`, with explicit negative test
files. `tests/stage2_return_inference.noqeri` has nine direct
self-hosted semantic checks.

The CI rebuild gate generates a fresh Stage-1 compiler from current source,
uses it to compile Stage-2 and Stage-3, checks byte equality, compiles the
same fixture in Stage-2, then compares its result against Stage-0 and native
x86-64 AOT. This does not claim all Noqeri-written compiler features have
native self-hosting.

## Genuinely growable collections

`Lib/std/owned_collections.noqeri` provides separate hosted-native
**integer** list, map and set adapters. They use an opaque handle to the
runtime in `libnoqeri_owned_collections.a` and automatically allocate more
storage as elements are appended or inserted. No caller-provided fixed
buffer, explicit reserve operation, or fixed-array size is required.

```noqeri
import "std/owned_collections"
let items: OwnedIntList
if owned_list_open(&items) >= 0 {
    owned_list_append(&items, 10)
    owned_list_append(&items, 20)
    let value: Outcome<int>
    owned_list_get(&items, 1, &value)
    // Use outcome_is_ok/value_or and close when done.
    owned_list_close(&items)
}
```

`owned_map_open/put/get/remove/close` and
`owned_set_open/add/get/remove/close` follow the same convention.
Handles are validated, stale handles cannot be reused, and memory is
freed on close. Growth is bounded to 1,000,000 elements per collection,
with a negative status on allocation failure. The runtime synchronizes
hosted access with a mutex. `owned_list_get` and companions use a typed
`Outcome<int>` to distinguish missing entries from zero.

For Linux x86-64 native AOT builds, explicitly build/link the runtime:

```sh
cmake --build build --target noqeri_owned_collections
node scripts/native-build.mjs tests/owned_collections_native.noqeri \
     build/owned-app --compiler=build/noqeri --owned-collections
```

**Not yet equivalent to a generic allocator-owned `List<T>`**: these
typed adapters currently store signed integer keys/values only, require
an explicit close, and are not implemented in the portable NQRI VM or
freestanding backends. The existing generic `List<T>` and `Map<K,V>`
remain caller-owned with `list_grow_into` requiring new storage. Generic
owned values, destruction semantics, handle lifetime verification, and
`[...]` automatically becoming growable are separate compiler/runtime
work.

## Typed native error propagation

`Lib/std/native_outcome.noqeri` defines wrappers returning results through
`Outcome<T>` output parameters. All wrappers initialize their outputs,
including failures, with an error **code, domain and message**. The
underlying file/fs/HTTP/JSON/NoeSQL/AI APIs remain available.

Examples include `native_file_open_read`, `native_file_read`,
`native_fs_mkdir`, `native_http_get`,
`native_http_response_status`, `native_json_count`,
`native_database_ready`, `native_database_begin`,
`native_ai_gemm`, `native_ai_softmax`, and `native_ai_sgd`.

`native_propagate_error` preserves code, domain and message when
transferring a failed `Outcome<T>` to a different
`Outcome<U>`, without assuming that the fallback value is success.
HTTP response codes such as 404 are a valid transport response and
must be explicitly interpreted by the caller. A NoeSQL failure must
not be disguised as an empty successful string.

The Stage-0 reference interpreter now supports typed string fields in
aggregate records by retaining owned C-string data and rejecting
unmanaged pointers. `tests/interpreter_string_fields.noqeri` and
`tests/typed_native_outcomes.noqeri` validate string error context
and propagation.

**Not yet a universal language-level typed `?` operator:** callers
still pass output references and check `Outcome<T>` explicitly;
not every native host operation exposes a detailed provider error,
and linking all native providers is not automatic on every target.

## Test commands

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target noqeri noqeri-nqri noqeri_owned_collections noqeri-owned-collections-test
python3 tests/check_stage2_inference.py ./build/noqeri
ctest --test-dir build -R '^noqeri-owned-collections$' --output-on-failure
./build/noqeri run tests/interpreter_string_fields.noqeri --unsafe
./build/noqeri run tests/typed_native_outcomes.noqeri --unsafe
```

See `.github/workflows/stage2-inference-owned.yml` for the complete
reproducible compiler and native AOT qualification chain.
