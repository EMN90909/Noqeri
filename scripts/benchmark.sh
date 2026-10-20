#!/usr/bin/env sh
set -eu

if [ ! -x ./build/noqeri ]; then
  echo "build/noqeri is required; run ./scripts/build.sh first" >&2
  exit 1
fi

mkdir -p build
bench=build/compiler_benchmark.noqeri
{
  echo 'module benchmark.compiler'
  echo 'function seed(value: int): int { return value + 1 }'
  i=0
  while [ "$i" -lt 500 ]; do
    echo "function f$i(value: int): int { return value + $i }"
    i=$((i + 1))
  done
  echo 'let result: int = f499(seed(1))'
  echo 'print(result)'
} > "$bench"

# Warm caches and compiler startup paths before timing.
./build/noqeri check "$bench" >/dev/null
./build/noqeri run "$bench" >/dev/null
./build/noqeri build "$bench" build/compiler_benchmark.s >/dev/null

now_ns() {
  date +%s%N
}

measure() {
  label="$1"
  iterations="$2"
  shift 2
  start="$(now_ns)"
  i=0
  while [ "$i" -lt "$iterations" ]; do
    "$@" >/dev/null
    i=$((i + 1))
  done
  end="$(now_ns)"
  elapsed_ns=$((end - start))
  total_ms=$((elapsed_ns / 1000000))
  avg_us=$((elapsed_ns / iterations / 1000))
  printf 'benchmark %-7s iterations=%s total_ms=%s avg_us=%s\n' "$label" "$iterations" "$total_ms" "$avg_us"
}

lines="$(wc -l < "$bench" | tr -d ' ')"
bytes="$(wc -c < "$bench" | tr -d ' ')"
echo "benchmark source functions=501 lines=$lines bytes=$bytes"
measure check 40 ./build/noqeri check "$bench"
measure run 40 ./build/noqeri run "$bench"
measure build 20 ./build/noqeri build "$bench" build/compiler_benchmark.s
