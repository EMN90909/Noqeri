#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
COMPILER="${NOQERI_NATIVE_COMPILER:-$ROOT/build-native-compiler/noqeri-native}"
PROOF="${NOQERI_NATIVE_PROOF:-$ROOT/build-native-compiler/native-compiler-proof.txt}"
OUT="${NOQERI_BENCHMARK_BUILD:-$ROOT/build/benchmarks/native}"
CC="${CC:-cc}"
CXX="${CXX:-c++}"

if [ ! -x "$COMPILER" ]; then
  echo "native compiler not found: $COMPILER" >&2
  echo "run: sh scripts/bootstrap-native-compiler.sh" >&2
  exit 2
fi
if [ ! -f "$PROOF" ]; then
  echo "native compiler qualification proof not found: $PROOF" >&2
  exit 2
fi
grep -qx 'qualified=true' "$PROOF"
grep -qx 'fixed_point=true' "$PROOF"
grep -qx 'steady_state_engine=selfhost-native-linux-x86_64' "$PROOF"
grep -qx 'vm_required_for_steady_state=false' "$PROOF"

mkdir -p "$OUT"

build_runtime() {
  name=$1
  source=$2
  assembly="$OUT/$name.s"
  object="$OUT/$name.o"
  binary="$OUT/$name"
  "$COMPILER" "$source" "$assembly" 3
  "$CC" -c "$assembly" -o "$object"
  "$CXX" "$ROOT/Runtime/native/native_program_host.cpp" "$object" -o "$binary"
}

build_runtime startup "$ROOT/Benchmarks/workloads/startup.noqeri"

echo "Prepared fixed-point-qualified native benchmark artifacts in $OUT"
echo "Copy Benchmarks/commands.example.json to Benchmarks/commands.json before running the suite."
