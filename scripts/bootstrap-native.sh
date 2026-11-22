#!/usr/bin/env bash
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
BUILD_DIR="${BUILD_DIR:-$ROOT/build-selfhost-native}"
SEED="$ROOT/Compiler/selfhost/trusted-stage2-native.o"
PIN_FILE="$ROOT/Compiler/selfhost/trusted-stage2-native.sha256"
RT_SOURCE="$ROOT/Runtime/native/selfhost_native_rt.c"
MANIFEST="$ROOT/Compiler/selfhost/native-codegen-sources.txt"
PROOF_TXT="$BUILD_DIR/native-selfhost-proof.txt"
PROOF_JSON="$BUILD_DIR/native-selfhost-proof.json"

echo "=== Noqeri Native Self-Hosting Bootstrap and Reproducibility Verification ==="

if [ ! -f "$SEED" ] || [ ! -f "$PIN_FILE" ]; then
    echo "Error: Stage-2 seed ($SEED) or pin file ($PIN_FILE) missing." >&2
    exit 2
fi

expected_pin=$(awk 'NF { print $1; exit }' "$PIN_FILE" | tr 'ABCDEF' 'abcdef')
actual_pin=$(sha256sum "$SEED" | awk '{print $1}' | tr 'ABCDEF' 'abcdef')
if [ "$expected_pin" != "$actual_pin" ]; then
    echo "Error: Stage-2 seed SHA-256 pin mismatch: $actual_pin != $expected_pin" >&2
    exit 2
fi
echo "Stage-2 seed SHA-256 pin verified: $actual_pin"

mkdir -p "$BUILD_DIR/stage3" "$BUILD_DIR/stage4" "$BUILD_DIR/stage5"

echo "--- Linking Stage-2 Native Compiler ---"
gcc -O2 -no-pie -z noexecstack -Wl,--build-id=none "$RT_SOURCE" "$SEED" -o "$BUILD_DIR/noqeri-stage2-native"
"$BUILD_DIR/noqeri-stage2-native" --selftest

echo "--- Stage 2 Compiling Stage 3 Object ---"
"$BUILD_DIR/noqeri-stage2-native" compile-manifest "$ROOT" "$MANIFEST" "$BUILD_DIR/stage3/compiler.o"

echo "--- Linking Stage-3 Native Compiler ---"
(cd "$BUILD_DIR/stage3" && gcc -O2 -no-pie -z noexecstack -Wl,--build-id=none "$RT_SOURCE" compiler.o -o "$BUILD_DIR/noqeri-stage3-native")
"$BUILD_DIR/noqeri-stage3-native" --selftest

echo "--- Stage 3 Compiling Stage 4 Object ---"
"$BUILD_DIR/noqeri-stage3-native" compile-manifest "$ROOT" "$MANIFEST" "$BUILD_DIR/stage4/compiler.o"

echo "--- Linking Stage-4 Native Compiler ---"
(cd "$BUILD_DIR/stage4" && gcc -O2 -no-pie -z noexecstack -Wl,--build-id=none "$RT_SOURCE" compiler.o -o "$BUILD_DIR/noqeri-stage4-native")
"$BUILD_DIR/noqeri-stage4-native" --selftest

echo "--- Stage 4 Compiling Stage 5 Object ---"
"$BUILD_DIR/noqeri-stage4-native" compile-manifest "$ROOT" "$MANIFEST" "$BUILD_DIR/stage5/compiler.o"

echo "--- Linking Stage-5 Native Compiler ---"
(cd "$BUILD_DIR/stage5" && gcc -O2 -no-pie -z noexecstack -Wl,--build-id=none "$RT_SOURCE" compiler.o -o "$BUILD_DIR/noqeri-stage5-native")
"$BUILD_DIR/noqeri-stage5-native" --selftest

echo "--- Verifying Bit-for-Bit Identity ---"
stage4_obj_sha=$(sha256sum "$BUILD_DIR/stage4/compiler.o" | awk '{print $1}')
stage5_obj_sha=$(sha256sum "$BUILD_DIR/stage5/compiler.o" | awk '{print $1}')
echo "Stage 4 Object SHA-256: $stage4_obj_sha"
echo "Stage 5 Object SHA-256: $stage5_obj_sha"

if ! cmp -s "$BUILD_DIR/stage4/compiler.o" "$BUILD_DIR/stage5/compiler.o"; then
    echo "ERROR: Stage 4 and Stage 5 ELF object outputs differ!" >&2
    exit 1
fi
echo "OK: Stage 4 and Stage 5 ELF object files are 100% bit-for-bit identical."

stage4_bin_sha=$(sha256sum "$BUILD_DIR/noqeri-stage4-native" | awk '{print $1}')
stage5_bin_sha=$(sha256sum "$BUILD_DIR/noqeri-stage5-native" | awk '{print $1}')
echo "Stage 4 Executable SHA-256: $stage4_bin_sha"
echo "Stage 5 Executable SHA-256: $stage5_bin_sha"

if ! cmp -s "$BUILD_DIR/noqeri-stage4-native" "$BUILD_DIR/noqeri-stage5-native"; then
    echo "ERROR: Stage 4 and Stage 5 linked executables differ!" >&2
    exit 1
fi
echo "OK: Stage 4 and Stage 5 linked executables are 100% bit-for-bit identical."

echo "--- Compiling and Running Validation Program with Stage-4 Compiler ---"
printf 'export function main(): i64 { return 42 as i64 }\n' > "$BUILD_DIR/validation.noqeri"
"$BUILD_DIR/noqeri-stage4-native" compile "$BUILD_DIR/validation.noqeri" "$BUILD_DIR/validation.o"
gcc -no-pie -z noexecstack "$BUILD_DIR/validation.o" -o "$BUILD_DIR/validation_bin"
set +e
"$BUILD_DIR/validation_bin"
val_exit=$?
set -e
if [ "$val_exit" -ne 42 ]; then
    echo "ERROR: Validation program failed with exit code $val_exit (expected 42)" >&2
    exit 1
fi
echo "OK: Validation program executed successfully with expected exit code 42."

{
  echo "schema=3"
  echo "execution_model=native-x86_64"
  echo "artifact_format=elf64"
  echo "native_codegen=true"
  echo "native_selfhost=true"
  echo "verified=true"
  echo "seed=$SEED"
  echo "seed_sha256=$actual_pin"
  echo "stage2_pin_verified=true"
  echo "stage4_obj_sha256=$stage4_obj_sha"
  echo "stage5_obj_sha256=$stage5_obj_sha"
  echo "stage4_stage5_object_byte_equal=true"
  echo "stage4_bin_sha256=$stage4_bin_sha"
  echo "stage5_bin_sha256=$stage5_bin_sha"
  echo "stage4_stage5_executable_byte_equal=true"
  echo "validation_exit_code=42"
} > "$PROOF_TXT"

cat <<EOF > "$PROOF_JSON"
{
  "schema": 3,
  "execution_model": "native-x86_64",
  "artifact_format": "elf64",
  "native_codegen": true,
  "native_selfhost": true,
  "verified": true,
  "seed_sha256": "$actual_pin",
  "stage2_pin_verified": true,
  "stage4_obj_sha256": "$stage4_obj_sha",
  "stage5_obj_sha256": "$stage5_obj_sha",
  "stage4_stage5_object_byte_equal": true,
  "stage4_bin_sha256": "$stage4_bin_sha",
  "stage5_bin_sha256": "$stage5_bin_sha",
  "stage4_stage5_executable_byte_equal": true,
  "validation_exit_code": 42
}
EOF

echo "Native self-hosting qualification and reproducibility test PASSED."
echo "Proof: $PROOF_TXT"
