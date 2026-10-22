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
  echo "A C++ compiler is required for the thin native compiler I/O host." >&2
  exit 2
}

mkdir -p "$BUILD_DIR" "$PORTABLE_BUILD"

# First prove the repository-pinned portable compiler reaches its NQRI fixed point.
# This VM leg is bootstrap-only. No VM participates after the first native Stage-2 object exists.
BUILD_DIR="$PORTABLE_BUILD" sh "$ROOT/scripts/bootstrap-nqri.sh"

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
"$CXX" -no-pie "$HOST_OBJ" "$STAGE2_OBJ" -o "$STAGE2_BIN"

# From here onward no NQRI VM participates in compilation.
"$STAGE2_BIN" compile-manifest "$ROOT" "$MANIFEST" "$STAGE3_OBJ" 3
cmp -s "$STAGE2_OBJ" "$STAGE3_OBJ" || {
  echo "Native self-host FAILED: Stage-2 and Stage-3 ELF objects differ." >&2
  echo "Stage-2 SHA-256: $(sha256sum "$STAGE2_OBJ" | awk '{print $1}')" >&2
  echo "Stage-3 SHA-256: $(sha256sum "$STAGE3_OBJ" | awk '{print $1}')" >&2
  echo "First differing bytes (1-based offset, Stage-2 octal, Stage-3 octal):" >&2
  cmp -l "$STAGE2_OBJ" "$STAGE3_OBJ" | head -n 32 >&2 || true
  if command -v objcopy >/dev/null 2>&1; then
    objcopy --dump-section .text="$BUILD_DIR/stage2.text" "$STAGE2_OBJ" 2>/dev/null || true
    objcopy --dump-section .text="$BUILD_DIR/stage3.text" "$STAGE3_OBJ" 2>/dev/null || true
    if [ -f "$BUILD_DIR/stage2.text" ] && [ -f "$BUILD_DIR/stage3.text" ]; then
      echo "Stage-2 .text SHA-256: $(sha256sum "$BUILD_DIR/stage2.text" | awk '{print $1}')" >&2
      echo "Stage-3 .text SHA-256: $(sha256sum "$BUILD_DIR/stage3.text" | awk '{print $1}')" >&2
      cmp -s "$BUILD_DIR/stage2.text" "$BUILD_DIR/stage3.text" || {
        echo "First .text differences:" >&2
        cmp -l "$BUILD_DIR/stage2.text" "$BUILD_DIR/stage3.text" | head -n 32 >&2 || true
        first_text_diff=$(cmp -l "$BUILD_DIR/stage2.text" "$BUILD_DIR/stage3.text" | awk 'NR==1 { print $1 }')
        if [ -n "$first_text_diff" ] && command -v objdump >/dev/null 2>&1; then
          first_addr=$((first_text_diff - 1))
          start_addr=$((first_addr > 48 ? first_addr - 48 : 0))
          stop_addr=$((first_addr + 96))
          echo "Stage-2 disassembly around first .text difference (0x$(printf '%x' "$first_addr")):" >&2
          objdump -d --start-address="$start_addr" --stop-address="$stop_addr" "$STAGE2_OBJ" >&2 || true
          echo "Stage-3 disassembly around first .text difference:" >&2
          objdump -d --start-address="$start_addr" --stop-address="$stop_addr" "$STAGE3_OBJ" >&2 || true
        fi
      }
    fi
  fi
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
