#!/usr/bin/env sh
set -eu

NQ=${NQ:-./build/noqeri}
if [ ! -x "$NQ" ]; then
  ./scripts/build.sh
  NQ=./build/noqeri
fi

"$NQ" --version
"$NQ" stage2-status
"$NQ" selftest
"$NQ" check examples/hello.noqeri
"$NQ" check tests/compiler_stage2_core.noqeri
"$NQ" check tests/generic_records.noqeri
"$NQ" check tests/generic_collections.noqeri
"$NQ" check tests/stdlib_deep_foundations.noqeri
node --test tests/tooling/*.test.mjs
node scripts/stdlib-audit.mjs --json >/dev/null
node scripts/deep-foundations-gate.mjs
NQ="$NQ" NOQERI_RUNTIME_TESTS=${NOQERI_RUNTIME_TESTS:-0} sh ./scripts/test_maturity.sh

echo "Stage-2 core checks passed"
