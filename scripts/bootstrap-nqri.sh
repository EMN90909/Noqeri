#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
HOST_BUILD="${NOQERI_HOST_BUILD:-$BUILD_DIR/bootstrap-host}"
RUNTIME="${NOQERI_NQRI_RUNTIME:-}"
REPO_SEED="$ROOT/Compiler/selfhost/trusted-stage1.nqri"
REPO_PIN_FILE="$ROOT/Compiler/selfhost/trusted-stage1.sha256"
CANONICAL_PIN_FILE="$ROOT/Compiler/selfhost/stage1-seed.sha256"
EXTERNAL_SEED="${NOQERI_STAGE1_NQRI:-}"
SEED="${NOQERI_STAGE1_NQRI:-$REPO_SEED}"
PIN="${NOQERI_STAGE1_SHA256:-}"
PROVENANCE="$ROOT/Compiler/selfhost/stage1-seed.json"
PIN_SOURCE="environment"
SEED_ORIGIN="repository-committed"
SEED_PIN_VERIFIED=false
SEED_COMPILER=""
MANIFEST="$ROOT/Compiler/selfhost/stage2-sources.txt"
STAGE2="$BUILD_DIR/noqeri-stage2.nqri"
STAGE3="$BUILD_DIR/noqeri-stage3.nqri"
PROOF="$BUILD_DIR/nqri-selfhost-proof.txt"

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
    echo "Noqeri NQRI self-host bootstrap: unsupported host; set NOQERI_TARGET explicitly." >&2
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
    echo "cmake is required once to build the NQRI VM bootstrap host, or set NOQERI_NQRI_RUNTIME." >&2
    exit 2
  }
  cmake -S "$ROOT" -B "$HOST_BUILD" -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
  cmake --build "$HOST_BUILD" --target noqeri-nqri --config Release
  for candidate in "$HOST_BUILD/noqeri-nqri" "$HOST_BUILD/Release/noqeri-nqri"; do
    if [ -x "$candidate" ]; then RUNTIME="$candidate"; break; fi
  done
fi
if [ -z "$RUNTIME" ] || [ ! -x "$RUNTIME" ]; then
  echo "NQRI VM runtime was not produced; set NOQERI_NQRI_RUNTIME to noqeri-nqri." >&2
  exit 2
fi
if [ -n "$EXTERNAL_SEED" ]; then
  if [ ! -f "$SEED" ]; then
    echo "Supplied Stage-1 seed does not exist: $SEED" >&2
    exit 2
  fi
  PIN=$(printf '%s' "$PIN" | tr 'ABCDEF' 'abcdef')
  case "$PIN" in *[!0123456789abcdef]*) echo "External seed requires a valid SHA-256 pin." >&2; exit 2 ;; esac
  if [ "${#PIN}" -ne 64 ]; then
    echo "Externally supplied Stage-1 seed requires NOQERI_STAGE1_SHA256 (64 hex digits)." >&2
    exit 2
  fi
  SEED_ORIGIN="externally-pinned"
else
  if [ ! -f "$REPO_SEED" ] || [ ! -f "$REPO_PIN_FILE" ] || [ ! -f "$CANONICAL_PIN_FILE" ] || [ ! -f "$PROVENANCE" ]; then
    echo "Repository Stage-1 seed package is incomplete; require trusted-stage1.nqri, trusted-stage1.sha256, stage1-seed.sha256 and stage1-seed.json." >&2
    exit 2
  fi
  repo_pin=$(awk 'NF { print $1; exit }' "$REPO_PIN_FILE" | tr 'ABCDEF' 'abcdef')
  canonical_pin=$(awk 'NF { print $1; exit }' "$CANONICAL_PIN_FILE" | tr 'ABCDEF' 'abcdef')
  case "$repo_pin" in *[!0123456789abcdef]*) echo "Repository Stage-1 SHA-256 pin is invalid." >&2; exit 2 ;; esac
  case "$canonical_pin" in *[!0123456789abcdef]*) echo "Canonical Stage-1 SHA-256 pin is invalid." >&2; exit 2 ;; esac
  if [ "${#repo_pin}" -ne 64 ] || [ "${#canonical_pin}" -ne 64 ] || [ "$repo_pin" != "$canonical_pin" ]; then
    echo "Repository Stage-1 digest and canonical pin disagree." >&2
    exit 2
  fi
  if [ -n "$PIN" ] && [ "$(printf '%s' "$PIN" | tr 'ABCDEF' 'abcdef')" != "$repo_pin" ]; then
    echo "NOQERI_STAGE1_SHA256 cannot override the repository pin without NOQERI_STAGE1_NQRI." >&2
    exit 2
  fi
  SEED="$REPO_SEED"
  PIN="$repo_pin"
  PIN_SOURCE="$REPO_PIN_FILE"
fi

sha256_file() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'
  else shasum -a 256 "$1" | awk '{print $1}'; fi
}
seed_sha256=$(sha256_file "$SEED" | tr 'ABCDEF' 'abcdef')
if [ "$seed_sha256" != "$PIN" ]; then
  echo "Stage-1 seed SHA-256 does not match the required pin: $seed_sha256 != $PIN" >&2
  exit 2
fi
SEED_PIN_VERIFIED=true
# Never execute Stage 1 before its canonical or explicit SHA-256 pin succeeds.
"$RUNTIME" --selftest
if ! "$RUNTIME" has-export "$SEED" stage2CompileToNqri; then
  echo "Hash-pinned Stage-1 seed is incompatible: it must export stage2CompileToNqri." >&2
  exit 2
fi
"$RUNTIME" compile-manifest "$SEED" "$ROOT" "$MANIFEST" "$STAGE2" "$TARGET"
"$RUNTIME" compile-manifest "$STAGE2" "$ROOT" "$MANIFEST" "$STAGE3" "$TARGET"

if ! cmp -s "$STAGE2" "$STAGE3"; then
  echo "Noqeri NQRI self-host bootstrap FAILED: Stage-2 and Stage-3 NQRI outputs differ." >&2
  echo "Stage-2 SHA-256: $(sha256_file "$STAGE2")" >&2
  echo "Stage-3 SHA-256: $(sha256_file "$STAGE3")" >&2
  echo "First differing bytes (1-based offset, Stage-2 octal, Stage-3 octal):" >&2
  cmp -l "$STAGE2" "$STAGE3" | head -n 20 >&2 || true
  "$RUNTIME" diff "$STAGE2" "$STAGE3" || true
  exit 1
fi

seed_bytes=$(wc -c < "$SEED" | tr -d ' ')
stage_bytes=$(wc -c < "$STAGE2" | tr -d ' ')
stage2_sha256=$(sha256_file "$STAGE2")
stage3_sha256=$(sha256_file "$STAGE3")
seed_compiler_sha256=""
if [ -n "$SEED_COMPILER" ] && [ -f "$SEED_COMPILER" ]; then seed_compiler_sha256=$(sha256_file "$SEED_COMPILER"); fi
{
  echo "schema=3"\n  echo "execution_model=nqri-vm"\n  echo "artifact_format=nqri"\n  echo "native_codegen=false"\n  echo "native_selfhost=false"
  echo "verified=true"
  echo "runtime=$RUNTIME"
  echo "seed=$SEED"
  echo "seed_bytes=$seed_bytes"
  echo "seed_sha256=$seed_sha256"
  echo "seed_origin=$SEED_ORIGIN"
  echo "stage1_pin_verified=$SEED_PIN_VERIFIED"
  echo "stage1_pin_source=$PIN_SOURCE"
  echo "stage1_seed_provenance=$PROVENANCE"
  echo "stage1_independently_trusted=false"
  echo "source_manifest_sha256=$(sha256_file "$MANIFEST")"
  echo "seed_compiler_sha256=$seed_compiler_sha256"
  echo "runtime_sha256=$(sha256_file "$RUNTIME")"
  echo "seed_compiler=$SEED_COMPILER"
  echo "stage2=$STAGE2"
  echo "stage2_sha256=$stage2_sha256"
  echo "stage3=$STAGE3"
  echo "stage3_sha256=$stage3_sha256"
  echo "stage_bytes=$stage_bytes"
  echo "target=$TARGET"
  echo "byte_equal=true"
  echo "equivalent=true"
} > "$PROOF"

echo "Stage-1 SHA-256 pin verified before execution: $seed_sha256"
echo "NQRI Stage-2/Stage-3 byte-for-byte equality verified: $stage2_sha256"
echo "Proof: $PROOF"
