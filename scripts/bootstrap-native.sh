#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
HOST_BUILD="${NOQERI_HOST_BUILD:-$BUILD_DIR/bootstrap-host}"
RUNTIME="${NOQERI_NQO_RUNTIME:-}"
SEED="${NOQERI_STAGE1_NQO:-$BUILD_DIR/noqeri-stage1.nqo}"
MANIFEST="$ROOT/Compiler/selfhost/stage2-sources.txt"
STAGE2="$BUILD_DIR/noqeri-stage2.nqo"
STAGE3="$BUILD_DIR/noqeri-stage3.nqo"
PROOF="$BUILD_DIR/native-selfhost-proof.txt"

host_target() {
  os=$(uname -s 2>/dev/null || printf unknown)
  arch=$(uname -m 2>/dev/null || printf unknown)
  case "$os/$arch" in
    Linux/x86_64|Linux/amd64) printf 3 ;;
    Linux/aarch64|Linux/arm64) printf 4 ;;
    Darwin/x86_64|Darwin/amd64) printf 5 ;;
    Darwin/arm64|Darwin/aarch64) printf 6 ;;
    *) return 1 ;;
  esac
}

TARGET="${NOQERI_TARGET:-}"
if [ -z "$TARGET" ]; then
  TARGET=$(host_target) || {
    echo "Noqeri native self-host bootstrap: unsupported host; set NOQERI_TARGET explicitly." >&2
    exit 2
  }
fi

mkdir -p "$BUILD_DIR"
if [ -z "$RUNTIME" ]; then
  for candidate in "$BUILD_DIR/noqeri-nqo" "$HOST_BUILD/noqeri-nqo" "$HOST_BUILD/Release/noqeri-nqo"; do
    if [ -x "$candidate" ]; then RUNTIME="$candidate"; break; fi
  done
fi
if [ -z "$RUNTIME" ]; then
  command -v cmake >/dev/null 2>&1 || {
    echo "cmake is required once to build the native NQO2 bootstrap host, or set NOQERI_NQO_RUNTIME." >&2
    exit 2
  }
  cmake -S "$ROOT" -B "$HOST_BUILD" -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
  cmake --build "$HOST_BUILD" --target noqeri-nqo --config Release
  for candidate in "$HOST_BUILD/noqeri-nqo" "$HOST_BUILD/Release/noqeri-nqo"; do
    if [ -x "$candidate" ]; then RUNTIME="$candidate"; break; fi
  done
fi
if [ -z "$RUNTIME" ] || [ ! -x "$RUNTIME" ]; then
  echo "native NQO2 runtime was not produced; set NOQERI_NQO_RUNTIME to noqeri-nqo." >&2
  exit 2
fi
if [ ! -f "$SEED" ]; then
  cat >&2 <<MSG
A trusted Stage-1 NQO2 compiler seed is required.
Set NOQERI_STAGE1_NQO to the seed artifact. After this trust boundary, Stage 1
builds Stage 2 and Stage 2 rebuilds Stage 3 entirely through the native NQO2 host.
MSG
  exit 2
fi

"$RUNTIME" --selftest
if ! "$RUNTIME" has-export "$SEED" stage2CompileToNqo; then
  echo "Trusted Stage-1 seed is incompatible: it must export stage2CompileToNqo." >&2
  exit 2
fi
"$RUNTIME" compile-manifest "$SEED" "$ROOT" "$MANIFEST" "$STAGE2" "$TARGET"
"$RUNTIME" compile-manifest "$STAGE2" "$ROOT" "$MANIFEST" "$STAGE3" "$TARGET"

if ! cmp -s "$STAGE2" "$STAGE3"; then
  echo "Noqeri self-host bootstrap FAILED: Stage-2 and Stage-3 NQO2 outputs differ." >&2
  exit 1
fi

seed_bytes=$(wc -c < "$SEED" | tr -d ' ')
stage_bytes=$(wc -c < "$STAGE2" | tr -d ' ')
{
  echo "verified=true"
  echo "runtime=$RUNTIME"
  echo "seed=$SEED"
  echo "seed_bytes=$seed_bytes"
  echo "stage2=$STAGE2"
  echo "stage3=$STAGE3"
  echo "stage_bytes=$stage_bytes"
  echo "target=$TARGET"
  echo "equivalent=true"
} > "$PROOF"

echo "Noqeri native self-host bootstrap verified."
echo "Stage 2 and Stage 3 are byte-identical: $STAGE2"
echo "Proof: $PROOF"
