#!/usr/bin/env sh
set -eu

./scripts/test_core.sh

expected='4
3
42
42
43
9
-4'
actual="$(./build/noqeri run tests/general_capabilities.noqeri)"
if [ "$actual" != "$expected" ]; then
  echo "general capability semantics mismatch" >&2
  printf 'expected:\n%s\nactual:\n%s\n' "$expected" "$actual" >&2
  exit 1
fi

reject_file() {
  file="$1"
  label="$2"
  if ./build/noqeri check "$file" >/dev/null 2>&1; then
    echo "checker accepted invalid program: $label" >&2
    exit 1
  fi
}

cat > build/invalid_type.noqeri <<'EOF'
let count: u32 = "not a number"
EOF
reject_file build/invalid_type.noqeri "string assigned to u32"

cat > build/invalid_const.noqeri <<'EOF'
const answer: int = 42
answer = 43
EOF
reject_file build/invalid_const.noqeri "const reassignment"

cat > build/invalid_condition.noqeri <<'EOF'
if 1 {
    print(1)
}
EOF
reject_file build/invalid_condition.noqeri "integer used as bool condition"

cat > build/invalid_pointer.noqeri <<'EOF'
let word: u32 = 1
let wordPtr: *u32 = &word
let bytePtr: *u8 = wordPtr
EOF
reject_file build/invalid_pointer.noqeri "incompatible pointer assignment"

cat > build/invalid_atomic.noqeri <<'EOF'
let value: int = atomic.load(7)
EOF
reject_file build/invalid_atomic.noqeri "atomic operation without pointer"

cat > build/invalid_generic.noqeri <<'EOF'
function same<T>(left: T, right: T): T {
    return left
}
print(same(1, "one"))
EOF
reject_file build/invalid_generic.noqeri "generic type disagreement"

cat > build/invalid_generic_constraint.noqeri <<'EOF'
function twice<T: Numeric>(value: T): T {
    return value + value
}
print(twice("unsafe"))
EOF
reject_file build/invalid_generic_constraint.noqeri "string violates Numeric generic constraint"

cat > build/invalid_narrowing.noqeri <<'EOF'
let wide: u64 = 300
let byte: u8 = wide
EOF
reject_file build/invalid_narrowing.noqeri "implicit u64 to u8 narrowing"

cat > build/invalid_literal_range.noqeri <<'EOF'
let byte: u8 = 300
EOF
reject_file build/invalid_literal_range.noqeri "out-of-range integer literal"

cat > build/invalid_signedness.noqeri <<'EOF'
let signed: i32 = -1
let unsigned: u32 = signed
EOF
reject_file build/invalid_signedness.noqeri "implicit signed to unsigned conversion"

cat > build/invalid_slice_escape.noqeri <<'EOF'
function bad(): []u32 {
    let local: [u32; 2] = [1 as u32, 2 as u32]
    let view: []u32 = slice(local)
    return view
}
EOF
reject_file build/invalid_slice_escape.noqeri "slice borrowed from local array escapes function"

cat > build/valid_integer_safety.noqeri <<'EOF'
let port: u16 = 8080
let small: u8 = 42
let wider: u32 = small
let signedWide: i64 = wider
print(port)
print(signedWide)
EOF
./build/noqeri check build/valid_integer_safety.noqeri >/dev/null
valid_output="$(./build/noqeri run build/valid_integer_safety.noqeri)"
if [ "$valid_output" != '8080
42' ]; then
  echo "safe integer conversion regression" >&2
  exit 1
fi

# String literals are decoded by the language frontend before interpretation/codegen.
cat > build/string_escapes.noqeri <<'EOF'
print("{\"status\":\"ok\"}")
print("line\nbreak")
EOF
string_output="$(./build/noqeri run build/string_escapes.noqeri)"
if [ "$string_output" != '{"status":"ok"}
line
break' ]; then
  echo "string escape decoding regression" >&2
  printf 'actual:\n%s\n' "$string_output" >&2
  exit 1
fi

# Constrained generics must produce concrete per-type instances and preserve behavior.
generic_output="$(./build/noqeri run tests/generics_constraints.noqeri)"
if [ "$generic_output" != '9
42' ]; then
  echo "constrained generic semantics regression" >&2
  exit 1
fi
./build/noqeri nir tests/generics_constraints.noqeri > build/generics.nir
grep -q 'function max__int' build/generics.nir
grep -q 'function twice__int' build/generics.nir
if grep -q 'function max<T>' build/generics.nir; then
  echo "unspecialized generic leaked into final NIR" >&2
  exit 1
fi

# Web output is produced from the checked/monomorphized program and must be static ES module code.
./build/noqeri web tests/web_module.noqeri build/web_module.mjs >/dev/null
grep -q '^export function apiStatus' build/web_module.mjs
grep -q '^export function greeting' build/web_module.mjs
if grep -Eq '\beval\s*\(|new Function\s*\(' build/web_module.mjs; then
  echo "web backend emitted dynamic code execution" >&2
  exit 1
fi
if command -v node >/dev/null 2>&1; then
  node --check build/web_module.mjs
fi
cat > build/web_json.noqeri <<'EOF'
export function payload(): string {
    return "{\"status\":\"ok\"}"
}
EOF
./build/noqeri web build/web_json.noqeri build/web_json.mjs >/dev/null
if command -v node >/dev/null 2>&1; then
  node --input-type=module -e 'import("./build/web_json.mjs").then(m => { const v=JSON.parse(m.payload()); if(v.status!=="ok") process.exit(1) })'
fi
cat > build/invalid_web_native.noqeri <<'EOF'
export function bad(): void {
    asm("nop")
}
EOF
./build/noqeri check build/invalid_web_native.noqeri >/dev/null
if ./build/noqeri web build/invalid_web_native.noqeri build/invalid_web_native.mjs >/dev/null 2>&1; then
  echo "web backend accepted native-only asm capability" >&2
  exit 1
fi

# NoqeriDB must execute CRUD, enforce key uniqueness, and persist data between scripts.
rm -f build/noqeridb-smoke.nqdb
cat > build/noqeridb_smoke.nqd <<'EOF'
table users {
    id: int key,
    name: text required,
    active: bool required
}
delete users
insert users { id: 1, name: "Ada", active: true }
insert users { id: 2, name: "Linus", active: false }
update users set { name: "Ada Lovelace" } where id = 1
select users where active = true
EOF
db_output="$(./build/noqeri db build/noqeridb_smoke.nqd build/noqeridb-smoke.nqdb)"
expected_db="$(printf 'id\tname\tactive\n1\tAda Lovelace\ttrue')"
if [ "$db_output" != "$expected_db" ]; then
  echo "NoqeriDB CRUD output regression" >&2
  printf 'expected:\n%s\nactual:\n%s\n' "$expected_db" "$db_output" >&2
  exit 1
fi
cat > build/noqeridb_readback.nqd <<'EOF'
select users where id = 2
EOF
readback="$(./build/noqeri db build/noqeridb_readback.nqd build/noqeridb-smoke.nqdb)"
expected_readback="$(printf 'id\tname\tactive\n2\tLinus\tfalse')"
if [ "$readback" != "$expected_readback" ]; then
  echo "NoqeriDB persistence regression" >&2
  exit 1
fi
cat > build/noqeridb_duplicate.nqd <<'EOF'
insert users { id: 2, name: "Duplicate", active: true }
EOF
if ./build/noqeri db build/noqeridb_duplicate.nqd build/noqeridb-smoke.nqdb >/dev/null 2>&1; then
  echo "NoqeriDB accepted duplicate key" >&2
  exit 1
fi

# The formatter must preserve both comment forms.
cat > build/comment_format.noqeri <<'EOF'
// line comment
let value:int=1 /* block comment */
print(value)
EOF
./build/noqeri format build/comment_format.noqeri >/dev/null
grep -q '// line comment' build/comment_format.noqeri
grep -q '/\* block comment \*/' build/comment_format.noqeri

# project.nqon is parsed structurally and lock v2 never falls back to FNV.
node --test tests/nqon.test.mjs
node tests/package_manager_host.mjs
mkdir -p build/manifest-test/src
cat > build/manifest-test/project.nqon <<'EOF'
name = manifest-test
version = "0.1.0"
edition = "2026"
entry = src/main.noqeri
profile = app
target = x86_64-unknown-none
registry = "https://github.com/EMN90909/noqeri-registry"
EOF
./build/noqeri manifest build/manifest-test/project.nqon | grep -q 'edition=2026'
./build/noqeri lock build/manifest-test/project.nqon >/dev/null
grep -q '^noqeri-lock 2if grep -qi 'fnv' build/manifest-test/noqeri.lock; then
  echo "legacy non-content lock hash returned" >&2
  exit 1
fi

./build/noqeri targets | grep -q '^x86_64-unknown-none '
./build/noqeri targets | grep -q '^aarch64-unknown-none '
./build/noqeri --version | grep -q '^edition=2026$'

./build/noqeri build examples/native_hello.noqeri build/native_hello.s
test -s build/native_hello.s
grep -q '^\.global noqeri_entry$' build/native_hello.s
grep -q '^noqeri_entry:$' build/native_hello.s

./build/noqeri build tests/systems_types.noqeri build/systems_types.s
test -s build/systems_types.s
grep -q '^\.global kernel_main$' build/systems_types.s
grep -q '^kernel_main:$' build/systems_types.s
grep -q 'mov DWORD PTR \[rax\], ebx' build/systems_types.s

./build/noqeri build examples/general_systems.noqeri build/general_systems.s
test -s build/general_systems.s
grep -q 'xchg QWORD PTR \[rax\], rbx' build/general_systems.s
grep -q 'pause' build/general_systems.s
grep -q '^exercise:$' build/general_systems.s

if grep -Eq '\bsyscall\b|\b_start\b|elf_x86_64|\.note\.GNU-stack' build/native_hello.s build/systems_types.s build/general_systems.s; then
  echo "freestanding native output contains a platform-specific runtime assumption" >&2
  exit 1
fi

case "$(uname -s)-$(uname -m)" in
  Linux-x86_64)
    ${CXX:-c++} -c build/native_hello.s -o build/native_hello.o
    ${CXX:-c++} -c build/systems_types.s -o build/systems_types.o
    ${CXX:-c++} -c build/general_systems.s -o build/general_systems.o
    test -s build/native_hello.o
    test -s build/systems_types.o
    test -s build/general_systems.o
    ;;
esac

echo "semantic, type-safety, constrained-generics, string, web, NoqeriDB, lifetime, package, formatter, target, and native codegen tests passed"
 build/manifest-test/noqeri.lock

# New projects expose only beginner-owned settings; compiler defaults stay implicit.
rm -rf build/new-project
./build/noqeri new starter build/new-project >/dev/null
grep -qx 'name = starter' build/new-project/project.nqon
grep -qx 'version = "0.1.0"' build/new-project/project.nqon
test "$(wc -l < build/new-project/project.nqon | tr -d ' ')" = "2"
grep -qx 'print("Hello from Noqeri")' build/new-project/src/main.noqeri
rm -rf build/new-project-spaced
./build/noqeri new "my app" build/new-project-spaced >/dev/null
grep -qx 'name = "my app"' build/new-project-spaced/project.nqon
if grep -qi 'fnv' build/manifest-test/noqeri.lock; then
  echo "legacy non-content lock hash returned" >&2
  exit 1
fi

./build/noqeri targets | grep -q '^x86_64-unknown-none '
./build/noqeri targets | grep -q '^aarch64-unknown-none '
./build/noqeri --version | grep -q '^edition=2026$'

./build/noqeri build examples/native_hello.noqeri build/native_hello.s
test -s build/native_hello.s
grep -q '^\.global noqeri_entry$' build/native_hello.s
grep -q '^noqeri_entry:$' build/native_hello.s

./build/noqeri build tests/systems_types.noqeri build/systems_types.s
test -s build/systems_types.s
grep -q '^\.global kernel_main$' build/systems_types.s
grep -q '^kernel_main:$' build/systems_types.s
grep -q 'mov DWORD PTR \[rax\], ebx' build/systems_types.s

./build/noqeri build examples/general_systems.noqeri build/general_systems.s
test -s build/general_systems.s
grep -q 'xchg QWORD PTR \[rax\], rbx' build/general_systems.s
grep -q 'pause' build/general_systems.s
grep -q '^exercise:$' build/general_systems.s

if grep -Eq '\bsyscall\b|\b_start\b|elf_x86_64|\.note\.GNU-stack' build/native_hello.s build/systems_types.s build/general_systems.s; then
  echo "freestanding native output contains a platform-specific runtime assumption" >&2
  exit 1
fi

case "$(uname -s)-$(uname -m)" in
  Linux-x86_64)
    ${CXX:-c++} -c build/native_hello.s -o build/native_hello.o
    ${CXX:-c++} -c build/systems_types.s -o build/systems_types.o
    ${CXX:-c++} -c build/general_systems.s -o build/general_systems.o
    test -s build/native_hello.o
    test -s build/systems_types.o
    test -s build/general_systems.o
    ;;
esac

echo "semantic, type-safety, constrained-generics, string, web, NoqeriDB, lifetime, package, formatter, target, and native codegen tests passed"
