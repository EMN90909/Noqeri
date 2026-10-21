#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
BUILD_DIR="${BUILD_DIR:-$ROOT/build-native-compiler}"
BOOTSTRAP_DIR="$BUILD_DIR/bootstrap"
MANIFEST="$ROOT/Compiler/native_selfhost/native-driver-sources.txt"
DRIVER_NQRI="$BUILD_DIR/noqeri-native-driver.nqri"
MERGED_SOURCE="$BUILD_DIR/noqeri-native-driver.merged.noqeri"
BOOTSTRAP_ASM="$BUILD_DIR/noqeri-native-bootstrap.s"
FIXED_ASM="$BUILD_DIR/noqeri-native-fixed.s"
COMPILER_OBJECT="$BUILD_DIR/noqeri-native-compiler.o"
NATIVE_COMPILER="$BUILD_DIR/noqeri-native"
PROOF="$BUILD_DIR/native-compiler-proof.txt"
TARGET=3
CXX="${CXX:-c++}"
CC="${CC:-cc}"

mkdir -p "$BUILD_DIR"

BUILD_DIR="$BOOTSTRAP_DIR" NOQERI_TARGET="$TARGET" sh "$ROOT/scripts/bootstrap-native.sh"

RUNTIME="${NOQERI_NQRI_RUNTIME:-}"
if [ -z "$RUNTIME" ]; then
  for candidate in     "$BOOTSTRAP_DIR/noqeri-nqri"     "$BOOTSTRAP_DIR/bootstrap-host/noqeri-nqri"     "$BOOTSTRAP_DIR/bootstrap-host/Release/noqeri-nqri"; do
    if [ -x "$candidate" ]; then RUNTIME="$candidate"; break; fi
  done
fi
if [ -z "$RUNTIME" ] || [ ! -x "$RUNTIME" ]; then
  echo "bootstrap NQRI runtime not found" >&2
  exit 2
fi

STAGE2="$BOOTSTRAP_DIR/noqeri-stage2.nqri"
if [ ! -f "$STAGE2" ]; then
  echo "Stage-2 compiler was not produced" >&2
  exit 2
fi

"$RUNTIME" compile-manifest "$STAGE2" "$ROOT" "$MANIFEST" "$DRIVER_NQRI" "$TARGET"
"$RUNTIME" has-export "$DRIVER_NQRI" nativeCompileToAssembly
"$RUNTIME" bundle-manifest "$ROOT" "$MANIFEST" "$MERGED_SOURCE"
"$RUNTIME" native-asm "$DRIVER_NQRI" "$MERGED_SOURCE" "$BOOTSTRAP_ASM" "$TARGET" 1

"$CC" -c "$BOOTSTRAP_ASM" -o "$COMPILER_OBJECT"
"$CXX" -O2 -pthread "$ROOT/Runtime/native/native_compiler_host.cpp" "$COMPILER_OBJECT" -o "$NATIVE_COMPILER"

engine=$("$NATIVE_COMPILER" --engine)
if [ "$engine" != "selfhost-native-linux-x86_64" ]; then
  echo "unexpected native compiler engine: $engine" >&2
  exit 1
fi

"$NATIVE_COMPILER" "$MERGED_SOURCE" "$FIXED_ASM" "$TARGET" 1
if ! cmp -s "$BOOTSTRAP_ASM" "$FIXED_ASM"; then
  echo "native compiler fixed-point FAILED: bootstrap and native-emitted assembly differ" >&2
  cmp -l "$BOOTSTRAP_ASM" "$FIXED_ASM" | head -n 20 >&2 || true
  exit 1
fi

sha256_file() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'
  else shasum -a 256 "$1" | awk '{print $1}'; fi
}

{
  echo "schema=1"
  echo "qualified=true"
  echo "target=linux-x86_64"
  echo "target_id=$TARGET"
  echo "bootstrap_host=nqri-vm"
  echo "steady_state_engine=selfhost-native-linux-x86_64"
  echo "native_driver=$DRIVER_NQRI"
  echo "native_compiler=$NATIVE_COMPILER"
  echo "bootstrap_assembly_sha256=$(sha256_file "$BOOTSTRAP_ASM")"
  echo "native_assembly_sha256=$(sha256_file "$FIXED_ASM")"
  echo "fixed_point=true"
  echo "vm_required_for_steady_state=false"
} > "$PROOF"

echo "Native compiler fixed-point verified."
echo "Compiler: $NATIVE_COMPILER"
echo "Proof: $PROOF"
