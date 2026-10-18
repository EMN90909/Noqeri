#!/usr/bin/env sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
STAGE0="${NOQERI_STAGE0:-}"
COMPILER="$ROOT/Compiler/selfhost/main.nqr"

if [ -z "$STAGE0" ]; then
  if command -v noqeri >/dev/null 2>&1; then STAGE0=$(command -v noqeri); fi
fi
if [ -z "$STAGE0" ]; then
  cat >&2 <<'MSG'
Noqeri's compiler is written in Noqeri.
A trusted Noqeri seed is required to produce the first local compiler artifact.
Set NOQERI_STAGE0 to that seed. The normal main-branch build does not use
CMake, C++, or a C++ compiler; C++ bootstrap provenance is archived separately.
MSG
  exit 2
fi
if ! command -v node >/dev/null 2>&1; then
  echo "Noqeri's portable Stage-2 host currently requires Node.js 20+" >&2
  exit 2
fi
if ! node -e 'process.exit(Number(process.versions.node.split(".")[0]) >= 20 ? 0 : 1)'; then
  echo "Noqeri's portable Stage-2 host requires Node.js 20 or newer" >&2
  exit 2
fi
if ! command -v "$STAGE0" >/dev/null 2>&1; then
  echo "Trusted NOQERI_STAGE0 seed not found or not executable: $STAGE0" >&2
  exit 2
fi
mkdir -p "$BUILD_DIR"
"$STAGE0" check "$COMPILER"
"$STAGE0" web "$COMPILER" "$BUILD_DIR/noqeri-stage2.mjs"
# The portable host imports a JavaScript ESM artifact; reject invalid seed output.
node --check "$BUILD_DIR/noqeri-stage2.mjs"
NOQERI_STAGE2_MODULE="$BUILD_DIR/noqeri-stage2.mjs" node "$ROOT/scripts/verify-stage2-module.mjs"
cat > "$BUILD_DIR/noqeri" <<LAUNCH
#!/usr/bin/env sh
NOQERI_STAGE2_MODULE="$BUILD_DIR/noqeri-stage2.mjs" exec node "$ROOT/Runtime/stage2-cli.mjs" "\$@"
LAUNCH
chmod +x "$BUILD_DIR/noqeri"
"$BUILD_DIR/noqeri" selftest
"$BUILD_DIR/noqeri" stage2-status
"$BUILD_DIR/noqeri" --version
printf 'Noqeri Stage-2 pipeline artifact built from Noqeri compiler source at %s\n' "$BUILD_DIR/noqeri"
printf 'Bootstrap equivalence remains governed separately by scripts/bootstrap-stage2.mjs.\n'
