#!/usr/bin/env sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
if ! command -v node >/dev/null 2>&1; then
  echo "NoeSQL reference tests require Node.js 22+" >&2
  exit 2
fi
node --test "$ROOT/tests/noesql.test.mjs"
node "$ROOT/Runtime/noesql/cli.mjs" check "$ROOT/tests/noesql_fixture.noe"
echo "NoeSQL reference checks completed; compiler-native .nqr tests require a trusted Noqeri Stage-0 seed."
