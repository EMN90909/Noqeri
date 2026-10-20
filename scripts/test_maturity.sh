#!/usr/bin/env sh
set -eu

NQ=${NQ:-./build/noqeri}
RUNTIME=${NOQERI_RUNTIME_TESTS:-0}
mkdir -p build

reject() {
  file="$1"
  label="$2"
  if "$NQ" check "$file" >/dev/null 2>&1; then
    echo "maturity checker accepted invalid program: $label" >&2
    exit 1
  fi
}

check_fixture() {
  file="$1"
  "$NQ" check "$file" >/dev/null
}

run_fixture() {
  file="$1"
  if [ "$RUNTIME" = "1" ]; then
    "$NQ" run "$file" >/dev/null
  fi
}

for fixture in \
  tests/generic_records.noqeri \
  tests/generic_collections.noqeri \
  tests/stdlib_catalog.noqeri \
  tests/stdlib_maturity.noqeri \
  tests/stdlib_structures.noqeri \
  tests/storage_structures.noqeri \
  tests/stdlib_foundations2.noqeri \
  tests/stdlib_deep_foundations.noqeri \
  tests/time_modules.noqeri \
  tests/data_integrity.noqeri \
  tests/encoding_foundations.noqeri \
  tests/repeat_loop.noqeri \
  tests/compiler_stage2_core.noqeri
do
  check_fixture "$fixture"
  run_fixture "$fixture"
done

cat > build/invalid_pointer_escape.noqeri <<'EOF_BAD_POINTER'
function bad_pointer(): *u32 {
    let local: u32 = 7 as u32
    return &local
}
EOF_BAD_POINTER
reject build/invalid_pointer_escape.noqeri "pointer borrowed from local storage escapes function"

cat > build/invalid_dangling_inner_reference.noqeri <<'EOF_BAD_INNER'
function bad_inner_reference(): u32 {
    let outer: u32 = 0 as u32
    let reference: *u32 = &outer
    {
        let inner: u32 = 9 as u32
        reference = &inner
    }
    return *reference
}
EOF_BAD_INNER
reject build/invalid_dangling_inner_reference.noqeri "reference outlives inner borrowed storage"

cat > build/invalid_static_array_index.noqeri <<'EOF_BAD_INDEX'
function invalid_index(): u32 {
    let values: [u32; 2] = [1 as u32, 2 as u32]
    return values[2]
}
EOF_BAD_INDEX
reject build/invalid_static_array_index.noqeri "known fixed-array index is out of bounds"

cat > build/invalid_direct_null_dereference.noqeri <<'EOF_BAD_NULL'
function invalid_null(): u32 {
    return *(null as *u32)
}
EOF_BAD_NULL
reject build/invalid_direct_null_dereference.noqeri "provable null dereference"

cat > build/valid_generic_record_nested.noqeri <<'EOF_GENERIC_NESTED'
record Cell<T> {
    value: T
}
record Envelope<T> {
    cell: Cell<T>
    revision: u64
}
function read_cell<T>(envelope: *Envelope<T>): T {
    return envelope.cell.value
}
export function read_int(envelope: *Envelope<int>): int {
    return read_cell(envelope)
}
EOF_GENERIC_NESTED
"$NQ" check build/valid_generic_record_nested.noqeri >/dev/null

cat > build/valid_generic_record_recursive.noqeri <<'EOF_GENERIC_RECURSIVE'
record Link<T> {
    value: T
    next: *Link<T>
}
function next_link<T>(link: *Link<T>): *Link<T> {
    return link.next
}
export function next_u64(link: *Link<u64>): *Link<u64> {
    return next_link(link)
}
EOF_GENERIC_RECURSIVE
"$NQ" check build/valid_generic_record_recursive.noqeri >/dev/null

if [ "$RUNTIME" = "1" ]; then
  echo "maturity compile and runtime checks passed"
else
  echo "maturity Stage-2 compile checks passed (runtime execution disabled; set NOQERI_RUNTIME_TESTS=1 when a runtime-capable toolchain is available)"
fi
