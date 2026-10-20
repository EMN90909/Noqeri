#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
HOST_BUILD="${NOQERI_HOST_BUILD:-$BUILD_DIR/bootstrap-host}"
RUNTIME="${NOQERI_NQRI_RUNTIME:-}"
SEED="${NOQERI_STAGE1_NQRI:-$BUILD_DIR/noqeri-stage1.nqri}"
SEED_COMPILER="${NOQERI_STAGE0_CPP:-}"
MANIFEST="$ROOT/Compiler/selfhost/stage2-sources.txt"
STAGE2="$BUILD_DIR/noqeri-stage2.nqri"
STAGE3="$BUILD_DIR/noqeri-stage3.nqri"
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
  for candidate in "$BUILD_DIR/noqeri-nqri" "$HOST_BUILD/noqeri-nqri" "$HOST_BUILD/Release/noqeri-nqri"; do
    if [ -x "$candidate" ]; then RUNTIME="$candidate"; break; fi
  done
fi
if [ -z "$RUNTIME" ]; then
  command -v cmake >/dev/null 2>&1 || {
    echo "cmake is required once to build the native NQRI bootstrap host, or set NOQERI_NQRI_RUNTIME." >&2
    exit 2
  }
  cmake -S "$ROOT" -B "$HOST_BUILD" -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
  cmake --build "$HOST_BUILD" --target noqeri-nqri noqeri --config Release
  for candidate in "$HOST_BUILD/noqeri-nqri" "$HOST_BUILD/Release/noqeri-nqri"; do
    if [ -x "$candidate" ]; then RUNTIME="$candidate"; break; fi
  done
fi
if [ -z "$RUNTIME" ] || [ ! -x "$RUNTIME" ]; then
  echo "native NQRI runtime was not produced; set NOQERI_NQRI_RUNTIME to noqeri-nqri." >&2
  exit 2
fi
if [ ! -f "$SEED" ]; then
  if [ -z "$SEED_COMPILER" ]; then
    for candidate in "$HOST_BUILD/noqeri" "$HOST_BUILD/Release/noqeri"; do
      if [ -x "$candidate" ]; then SEED_COMPILER="$candidate"; break; fi
    done
  fi
  if [ -z "$SEED_COMPILER" ] || [ ! -x "$SEED_COMPILER" ]; then
    command -v cmake >/dev/null 2>&1 || {
      echo "cmake is required to build the trusted C++ Stage-0 seed compiler." >&2
      exit 2
    }
    cmake -S "$ROOT" -B "$HOST_BUILD" -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
    cmake --build "$HOST_BUILD" --target noqeri noqeri-nqri --config Release
    for candidate in "$HOST_BUILD/noqeri" "$HOST_BUILD/Release/noqeri"; do
      if [ -x "$candidate" ]; then SEED_COMPILER="$candidate"; break; fi
    done
  fi
  if [ -z "$SEED_COMPILER" ] || [ ! -x "$SEED_COMPILER" ]; then
    echo "trusted C++ Stage-0 compiler was not produced; set NOQERI_STAGE0_CPP." >&2
    exit 2
  fi
  echo "Generating trusted Stage-1 NQRI seed with C++ Stage-0 compiler..."
  "$SEED_COMPILER" nqri "$ROOT/Compiler/selfhost/main.noqeri" "$SEED" --unsafe
fi

"$RUNTIME" --selftest
if ! "$RUNTIME" has-export "$SEED" stage2CompileToNqri; then
  echo "Trusted Stage-1 seed is incompatible: it must export stage2CompileToNqri." >&2
  exit 2
fi
"$RUNTIME" compile-manifest "$SEED" "$ROOT" "$MANIFEST" "$STAGE2" "$TARGET"
"$RUNTIME" compile-manifest "$STAGE2" "$ROOT" "$MANIFEST" "$STAGE3" "$TARGET"

if ! cmp -s "$STAGE2" "$STAGE3"; then
  echo "Noqeri self-host bootstrap FAILED: Stage-2 and Stage-3 NQRI outputs differ." >&2
  exit 1
fi

seed_bytes=$(wc -c < "$SEED" | tr -d ' ')
stage_bytes=$(wc -c < "$STAGE2" | tr -d ' ')
seed_sha256=$(sha256sum "$SEED" 2>/dev/null | awk '{print $1}' || shasum -a 256 "$SEED" | awk '{print $1}')
stage2_sha256=$(sha256sum "$STAGE2" 2>/dev/null | awk '{print $1}' || shasum -a 256 "$STAGE2" | awk '{print $1}')
stage3_sha256=$(sha256sum "$STAGE3" 2>/dev/null | awk '{print $1}' || shasum -a 256 "$STAGE3" | awk '{print $1}')
{
  echo "verified=true"
  echo "runtime=$RUNTIME"
  echo "seed=$SEED"
  echo "seed_bytes=$seed_bytes"
  echo "seed_sha256=$seed_sha256"
  echo "seed_compiler=$SEED_COMPILER"
  echo "stage2=$STAGE2"
  echo "stage2_sha256=$stage2_sha256"
  echo "stage3=$STAGE3"
  echo "stage3_sha256=$stage3_sha256"
  echo "stage_bytes=$stage_bytes"
  echo "target=$TARGET"
  echo "equivalent=true"
} > "$PROOF"

echo "Noqeri native self-host bootstrap verified."
echo "Stage 2 and Stage 3 are byte-identical: $STAGE2"
echo "Proof: $PROOF"
