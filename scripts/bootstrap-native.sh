#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
BUILD_DIR="${BUILD_DIR:-$ROOT/build-native-selfhost}"
PORTABLE_BUILD="$BUILD_DIR/portable"
MANIFEST="$ROOT/Compiler/selfhost/stage2-sources.txt"
STAGE2_OBJ="$BUILD_DIR/noqeri-stage2-native.o"
STAGE3_OBJ="$BUILD_DIR/noqeri-stage3-native.o"
STAGE4_OBJ="$BUILD_DIR/noqeri-stage4-native.o"
STAGE2_BIN="$BUILD_DIR/noqeri-stage2-native"
STAGE3_BIN="$BUILD_DIR/noqeri-stage3-native"
HOST_OBJ="$BUILD_DIR/native-compiler-host.o"
PROOF="$BUILD_DIR/native-selfhost-proof.txt"
CXX="${CXX:-c++}"

case "$(uname -s 2>/dev/null || true)/$(uname -m 2>/dev/null || true)" in
  Linux/x86_64|Linux/amd64) ;;
  *)
    echo "Native self-host qualification currently supports Linux x86-64 only." >&2
    exit 2
    ;;
esac

command -v "$CXX" >/dev/null 2>&1 || {
  echo "A C++ compiler is required only for the temporary bootstrap I/O host." >&2
  exit 2
}

mkdir -p "$BUILD_DIR" "$PORTABLE_BUILD"

# First refresh the portable compiler from the repository-pinned Stage-1 seed.
# This VM leg is bootstrap only. It must not appear in Stage-2 -> Stage-3.
BUILD_DIR="$PORTABLE_BUILD" NOQERI_BOOTSTRAP_STAGE2_ONLY=1 sh "$ROOT/scripts/bootstrap-nqri.sh"

PORTABLE="$PORTABLE_BUILD/noqeri-stage2.nqri"
[ -f "$PORTABLE" ] || { echo "portable Stage-2 compiler was not produced" >&2; exit 2; }

RUNTIME="${NOQERI_NQRI_RUNTIME:-}"
if [ -z "$RUNTIME" ]; then
  for candidate in     "$PORTABLE_BUILD/noqeri-nqri"     "$PORTABLE_BUILD/bootstrap-host/noqeri-nqri"     "$PORTABLE_BUILD/bootstrap-host/Release/noqeri-nqri"; do
    if [ -x "$candidate" ]; then RUNTIME="$candidate"; break; fi
  done
fi
[ -n "$RUNTIME" ] && [ -x "$RUNTIME" ] || {
  echo "NQRI bootstrap runtime not found after portable bootstrap." >&2
  exit 2
}

"$RUNTIME" has-export "$PORTABLE" stage2CompileToNativeObject >/dev/null

# The Noqeri compiler running in the pinned VM emits the first native compiler object.
"$RUNTIME" compile-native-manifest "$PORTABLE" "$ROOT" "$MANIFEST" "$STAGE2_OBJ" 3

# This host performs filesystem/manifest I/O only. Compiler semantics and native
# machine-code/object generation are in the linked Noqeri object.
"$CXX" -std=c++17 -O2 -c "$ROOT/Runtime/native/native_compiler_host.cpp" -o "$HOST_OBJ"
if command -v readelf >/dev/null 2>&1; then
  echo "Stage-2 exported symbol diagnostics:"
  readelf -Ws "$STAGE2_OBJ" | grep -E 'stage1VersionMajor|stage2SyntaxCheck|stage2CompileToNativeObject' || true
fi
"$CXX" -no-pie "$HOST_OBJ" "$STAGE2_OBJ" -o "$STAGE2_BIN"
if command -v nm >/dev/null 2>&1; then
  nm -n "$STAGE2_BIN" | grep -E 'stage1VersionMajor|stage2SyntaxCheck|stage2CompileToNativeObject' || true
fi

# From here onward no NQRI VM participates in compilation.
"$STAGE2_BIN" diagnose-manifest "$ROOT" "$MANIFEST"
"$STAGE2_BIN" compile-manifest "$ROOT" "$MANIFEST" "$STAGE3_OBJ" 3
cmp -s "$STAGE2_OBJ" "$STAGE3_OBJ" || {
  echo "Native self-host FAILED: Stage-2 and Stage-3 ELF objects differ." >&2
  exit 1
}

"$CXX" -no-pie "$HOST_OBJ" "$STAGE3_OBJ" -o "$STAGE3_BIN"
"$STAGE3_BIN" compile-manifest "$ROOT" "$MANIFEST" "$STAGE4_OBJ" 3
cmp -s "$STAGE3_OBJ" "$STAGE4_OBJ" || {
  echo "Native self-host FAILED: Stage-3 and Stage-4 ELF objects differ." >&2
  exit 1
}

sha256_file() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'
  else shasum -a 256 "$1" | awk '{print $1}'; fi
}

stage2_sha=$(sha256_file "$STAGE2_OBJ")
stage3_sha=$(sha256_file "$STAGE3_OBJ")
stage4_sha=$(sha256_file "$STAGE4_OBJ")
portable_sha=$(sha256_file "$PORTABLE")
host_sha=$(sha256_file "$HOST_OBJ")

{
  echo "schema=1"
  echo "verified=true"
  echo "target=linux-x86_64"
  echo "execution_model=native-aot-selfhost"
  echo "artifact_format=elf64-relocatable"
  echo "portable_bootstrap=nqri-vm"
  echo "native_codegen=true"
  echo "native_selfhost=true"
  echo "native_stage2_runs_without_vm=true"
  echo "native_stage3_runs_without_vm=true"
  echo "stage2_stage3_byte_equal=true"
  echo "stage3_stage4_byte_equal=true"
  echo "release_needs_cpp_host=true"
  echo "compiler_backend=Compiler/selfhost/x64_encoder.noqeri"
  echo "object_writer=Compiler/selfhost/elf_native_writer.noqeri"
  echo "portable_compiler=$PORTABLE"
  echo "portable_compiler_sha256=$portable_sha"
  echo "bootstrap_host_sha256=$host_sha"
  echo "stage2_object=$STAGE2_OBJ"
  echo "stage2_sha256=$stage2_sha"
  echo "stage3_object=$STAGE3_OBJ"
  echo "stage3_sha256=$stage3_sha"
  echo "stage4_object=$STAGE4_OBJ"
  echo "stage4_sha256=$stage4_sha"
} > "$PROOF"

echo "Native Noqeri compiler self-host verified."
echo "Stage-2/3/4 object SHA-256: $stage2_sha"
echo "Proof: $PROOF"
