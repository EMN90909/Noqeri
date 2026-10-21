#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
SEED="${NOQERI_STAGE1_NQRI:-$BUILD_DIR/noqeri-stage1.nqri}"
STAGE2="$BUILD_DIR/noqeri-stage2.nqri"
STAGE3="$BUILD_DIR/noqeri-stage3.nqri"
PROOF="$BUILD_DIR/native-selfhost-proof.txt"
PIN="$ROOT/Compiler/bootstrap/trusted-stage1.nqri"
HASH="$ROOT/Compiler/bootstrap/trusted-stage1.sha256"
INFO="$ROOT/Compiler/bootstrap/trusted-stage1-provenance.txt"

for input in "$SEED" "$STAGE2" "$STAGE3" "$PROOF"; do
  if [ ! -s "$input" ]; then echo "Missing bootstrap artifact: $input" >&2; exit 1; fi
done
grep -qx 'verified=true' "$PROOF" || { echo 'Bootstrap proof is not verified' >&2; exit 1; }
grep -qx 'equivalent=true' "$PROOF" || { echo 'Bootstrap proof is not equivalent' >&2; exit 1; }
cmp -s "$STAGE2" "$STAGE3" || { echo 'Stage-2 / Stage-3 bytes differ' >&2; exit 1; }

digest() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'
  else shasum -a 256 "$1" | awk '{print $1}'; fi
}
seed_hash=$(digest "$SEED")
stage2_hash=$(digest "$STAGE2")
stage3_hash=$(digest "$STAGE3")
grep -qx "seed_sha256=$seed_hash" "$PROOF" || { echo 'Seed hash disagrees with proof' >&2; exit 1; }
grep -qx "stage2_sha256=$stage2_hash" "$PROOF" || { echo 'Stage-2 hash disagrees with proof' >&2; exit 1; }
grep -qx "stage3_sha256=$stage3_hash" "$PROOF" || { echo 'Stage-3 hash disagrees with proof' >&2; exit 1; }
if [ "$stage2_hash" != "$stage3_hash" ]; then echo 'Rebuild digest mismatch' >&2; exit 1; fi

commit=$(git -C "$ROOT" rev-parse HEAD)
mkdir -p "$(dirname "$PIN")"
cp "$SEED" "$PIN"
printf '%s  trusted-stage1.nqri\n' "$seed_hash" > "$HASH"
{
  printf 'schema=1\n'
  printf 'source_commit=%s\n' "$commit"
  printf 'stage1_sha256=%s\n' "$seed_hash"
  printf 'stage2_sha256=%s\n' "$stage2_hash"
  printf 'stage3_sha256=%s\n' "$stage3_hash"
  printf 'bootstrap_runtime=native-nqri\n'
  printf 'trust_root=C++ Stage-0 compiler + native NQRI VM\n'
  printf 'equivalent=true\n'
} > "$INFO"
echo "Pinned Stage-1 at $PIN SHA-256=$seed_hash"
