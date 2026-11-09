#!/usr/bin/env sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
sh "$ROOT/scripts/bootstrap-nqri.sh" "$@"
if [ ! -x "$ROOT/build/noqeri" ] && [ ! -f "$ROOT/build/noqeri.exe" ]; then
  cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$ROOT/build" --config Release --target noqeri
fi

