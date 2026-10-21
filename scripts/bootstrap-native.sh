#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
HOST_BUILD="${NOQERI_HOST_BUILD:-$BUILD_DIR/bootstrap-host}"
RUNTIME="${NOQERI_NQRI_RUNTIME:-}"
SEED="${NOQERI_STAGE1_NQRI:-$BUILD_DIR/noqeri-stage1.nqri}"
EXTERNAL_SEED="${NOQERI_STAGE1_NQRI:-}"
PIN="${NOQERI_STAGE1_SHA256:-}"
SEED_ORIGIN="local-cpp-stage0"
SEED_COMPILER="${NOQERI_STAGE0_CPP:-}"
SEED_OVERRIDE="${NOQERI_STAGE1_NQRI:-}"
EXPECTED_SEED_SHA256="${NOQERI_STAGE1_SHA256:-}"
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
if [ -n "$EXTERNAL_SEED" ]; then
  if [ -n "$SEED_OVERRIDE" ]; then
  if [ ! -f "$SEED" ]; then
    echo "Supplied Stage-1 seed does not exist: $SEED" >&2
    exit 2
  fi
  if [ -z "$EXPECTED_SEED_SHA256" ]; then
    echo "Externally supplied Stage-1 seed requires NOQERI_STAGE1_SHA256 (64 hex digits)." >&2
    exit 2
  fi
else
  # Never reuse a previous build artifact as an implicitly trusted seed.
  # Regenerate from the checked Stage-0 C++ compiler on every invocation.
  rm -f "$SEED"
 echo "Explicit Stage-1 seed not found: $SEED" >&2; exit 2; fi
  if [ -z "$PIN" ]; then echo "External seed requires NOQERI_STAGE1_SHA256." >&2; exit 2; fi
  SEED_ORIGIN="externally-pinned"
else
  # Rebuild every time rather than trusting stale build bytes.
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
  echo "Generating local Stage-1 seed candidate with C++ Stage-0..."
  "$SEED_COMPILER" bootstrap-seed "$ROOT/Compiler/selfhost/main.noqeri" "$SEED"
  repro="$BUILD_DIR/noqeri-stage1-repro.nqri"
  "$SEED_COMPILER" bootstrap-seed "$ROOT/Compiler/selfhost/main.noqeri" "$repro"
  if ! cmp -s "$SEED" "$repro"; then
    echo "Stage-0 produced non-reproducible Stage-1 seed bytes." >&2; exit 1
  fi
  rm -f "$repro"
fi
sha256_file() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'
  else shasum -a 256 "$1" | awk '{print $1}'; fi
}
seed_sha256=$(sha256_file "$SEED")
if [ -n "$PIN" ]; then
  case "$PIN" in *[!0123456789abcdefABCDEF]*) echo "Invalid SHA-256 pin." >&2; exit 2 ;; esac
  if [ "${#PIN}" -ne 64 ] || [ "$seed_sha256" != "$(printf '%s' "$PIN" | tr 'ABCDEF' 'abcdef')" ]; then
    echo "Stage-1 seed SHA-256 does not match the supplied pin: $seed_sha256" >&2; exit 2
  fi
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
  echo "verified=true"
  echo "runtime=$RUNTIME"
  echo "seed=$SEED"
  echo "seed_bytes=$seed_bytes"
  echo "seed_sha256=$seed_sha256"
  echo "seed_origin=$SEED_ORIGIN"
  echo "stage1_independently_trusted=false"
  echo "source_manifest_sha256=$(sha256_file "$MANIFEST")"
  echo "seed_compiler_sha256=$seed_compiler_sha256"
  echo "seed_compiler=$SEED_COMPILER"
  echo "seed_origin=$(if [ -n "$SEED_OVERRIDE" ]; then printf external-pinned; else printf generated-stage0; fi)"
  echo "stage2=$STAGE2"
  echo "stage2_sha256=$stage2_sha256"
  echo "stage3=$STAGE3"
  echo "stage3_sha256=$stage3_sha256"
  echo "stage_bytes=$stage_bytes"
  echo "target=$TARGET"
  echo "equivalent=true"
} > "$PROOF"

echo "Native Stage-2/Stage-3 equivalence verified; independent Stage-1 trust NOT attested."
echo "Stage 2 and Stage 3 are byte-identical: $STAGE2"
echo "Proof: $PROOF"
