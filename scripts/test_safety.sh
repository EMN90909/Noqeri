#!/usr/bin/env sh
set -eu

NQ="${NQ:-./build/noqeri}"
mkdir -p build

reject_check() {
  file="$1"
  label="$2"
  if "$NQ" check "$file" >/dev/null 2>&1; then
    echo "safety checker accepted invalid program: $label" >&2
    exit 1
  fi
}

reject_run() {
  file="$1"
  label="$2"
  if "$NQ" run "$file" >/dev/null 2>&1; then
    echo "runtime accepted invalid program: $label" >&2
    exit 1
  fi
}

cat > build/unsafe_required.nqr <<'EOF'
function write_raw(ptr: *u32): void {
    ptr[0] = 7 as u32
}
EOF
reject_check build/unsafe_required.nqr "raw pointer indexing outside unsafe block"

cat > build/unsafe_allowed.nqr <<'EOF'
function write_raw(ptr: *u32): void {
    unsafe {
        ptr[0] = 7 as u32
    }
}
EOF
"$NQ" check build/unsafe_allowed.nqr >/dev/null

cat > build/dynamic_bounds_ok.nqr <<'EOF'
let values: [int; 3] = [10, 20, 30]
let index: usize = 1 as usize
print(values[index])
EOF
bounds_output="$("$NQ" run build/dynamic_bounds_ok.nqr)"
if [ "$bounds_output" != '20' ]; then
  echo "checked dynamic index changed valid semantics" >&2
  exit 1
fi
"$NQ" nir build/dynamic_bounds_ok.nqr > build/dynamic_bounds_ok.nir
grep -q 'check_bounds' build/dynamic_bounds_ok.nir

cat > build/dynamic_bounds_bad.nqr <<'EOF'
let values: [int; 3] = [10, 20, 30]
let index: usize = 9 as usize
print(values[index])
EOF
reject_run build/dynamic_bounds_bad.nqr "dynamic fixed-array index beyond length"

cat > build/null_runtime_bad.nqr <<'EOF'
let ptr: *u32 = null
unsafe {
    print(ptr[0])
}
EOF
reject_run build/null_runtime_bad.nqr "null raw-pointer index"

cat > build/overflow_checked_bad.nqr <<'EOF'
let max: i64 = 9223372036854775807
let one: i64 = 1
print(max + one)
EOF
if NOQERI_CHECKED_OVERFLOW=1 "$NQ" run build/overflow_checked_bad.nqr >/dev/null 2>&1; then
  echo "checked-overflow mode accepted i64 overflow" >&2
  exit 1
fi

# Aggregate field-path analysis must reject a field that keeps a borrow after
# the borrowed local leaves its scope.
cat > build/aggregate_borrow_escape.nqr <<'EOF'
record Holder {
    value: *i64
}
function bad(): int {
    let storage: [u8; 16] = [0 as u8,0 as u8,0 as u8,0 as u8,0 as u8,0 as u8,0 as u8,0 as u8,0 as u8,0 as u8,0 as u8,0 as u8,0 as u8,0 as u8,0 as u8,0 as u8]
    let holder: *Holder = (&storage[0 as usize]) as *Holder
    {
        let local: i64 = 7 as i64
        holder.value = &local
    }
    let escaped: *i64 = holder.value
    return 0
}
EOF
reject_check build/aggregate_borrow_escape.nqr "aggregate field retains borrow of expired local"

# Interprocedural summaries must carry escape information through a helper
# rather than only checking direct assignments in the caller.
cat > build/interprocedural_borrow_escape.nqr <<'EOF'
record Holder {
    value: *i64
}
function remember(holder: *Holder, value: *i64): void {
    holder.value = value
}
function bad(): int {
    let storage: [u8; 16] = [0 as u8,0 as u8,0 as u8,0 as u8,0 as u8,0 as u8,0 as u8,0 as u8,0 as u8,0 as u8,0 as u8,0 as u8,0 as u8,0 as u8,0 as u8,0 as u8]
    let holder: *Holder = (&storage[0 as usize]) as *Holder
    let local: i64 = 9 as i64
    remember(holder, &local)
    return 0
}
EOF
reject_check build/interprocedural_borrow_escape.nqr "callee summary stores borrow of caller local"

cat > build/web_checked_index.nqr <<'EOF'
export function pick(index: usize): int {
    let values: [int; 3] = [4, 8, 12]
    return values[index]
}
EOF
"$NQ" web build/web_checked_index.nqr build/web_checked_index.mjs >/dev/null
grep -q '__nqIndex' build/web_checked_index.mjs
if command -v node >/dev/null 2>&1; then
  node --input-type=module -e 'import("./build/web_checked_index.mjs").then(m => { if(m.pick(1)!==8) process.exit(1); let trapped=false; try { m.pick(9) } catch(e) { trapped=e instanceof RangeError } if(!trapped) process.exit(2) })'
fi

# Native codegen must preserve explicit NIR null and bounds traps.
cat > build/native_runtime_checks.nqr <<'EOF'
function native_checked_access(index: usize, ptr: *u32): u32 {
    let values: [u32; 3] = [1 as u32, 2 as u32, 3 as u32]
    let value: u32 = values[index]
    unsafe {
        return ptr[0] + value
    }
}
EOF
"$NQ" build build/native_runtime_checks.nqr build/native_runtime_checks.s >/dev/null
native_traps="$(grep -c 'ud2' build/native_runtime_checks.s || true)"
if [ "${native_traps:-0}" -lt 2 ]; then
  echo "native codegen dropped a null or bounds trap" >&2
  exit 1
fi

echo "unsafe-boundary, runtime-bounds, null-check, checked-overflow, aggregate-borrow, interprocedural-borrow, and web-safety tests passed"
