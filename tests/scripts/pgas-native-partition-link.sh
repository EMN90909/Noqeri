#!/usr/bin/env bash
set -euo pipefail
compiler="${1:?pass compiler path}"
if [ ! -x "$compiler" ]; then echo "compiler is not executable" >&2; exit 2; fi
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
"$compiler" build tests/pgas_partition_native.noqeri "$work/program.s" -O0
grep -q 'call nq_pgas_compiler_partition_read' "$work/program.s"
grep -q 'call nq_pgas_compiler_partition_write' "$work/program.s"
g++ -std=c++17 -Wall -Wextra -Werror -pthread -IInclude \
  "$work/program.s" Runtime/native/pgas_runtime.cpp Runtime/native/pgas_partition.cpp \
  tests/cpp/pgas_compiled_partition_test.cpp -o "$work/native_partition"
timeout 30s "$work/native_partition"
g++ -std=c++17 -Wall -Wextra -Werror -pthread -IInclude \
  "$work/program.s" Runtime/native/pgas_runtime.cpp Runtime/native/pgas_partition.cpp \
  tests/cpp/pgas_compiled_unbound_test.cpp -o "$work/unbound_partition"
ulimit -c 0 || true
if timeout 10s "$work/unbound_partition" > "$work/failure.txt" 2>&1; then
  echo "unbound partition incorrectly returned successfully" >&2
  exit 1
fi
grep -q 'NQR-PGAS401' "$work/failure.txt"
echo "PGAS native machine-code linking, routing and fail-stop: PASS"
