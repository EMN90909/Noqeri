#!/usr/bin/env bash
set -euo pipefail
compiler="${1:?pass compiler path}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
"$compiler" pgas-compile tests/pgas_remote_closure.noqeri "$work/function.nqpg"
test -s "$work/function.nqpg"
test "$(wc -c < "$work/function.nqpg")" -le 256
g++ -std=c++17 -Wall -Wextra -Werror -pthread -IInclude \
  Runtime/native/pgas_runtime.cpp tests/cpp/pgas_compiled_bytecode_test.cpp \
  -o "$work/bytecode_runner"
timeout 30s "$work/bytecode_runner" "$work/function.nqpg"
echo "PGAS restricted Noqeri closure target: PASS"
