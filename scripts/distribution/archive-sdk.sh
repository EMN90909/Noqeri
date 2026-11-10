#!/usr/bin/env bash
# Creates the complete portable Noqeri SDK archive (no compiler-only assets).
# Native installer generation is separate, and always uses the SAME build tree.
set -euo pipefail
NOQERI_RELEASE_TARGET="${NOQERI_RELEASE_TARGET:-${RELEASE_TARGET:-}}"
: "${NOQERI_RELEASE_TARGET:?set NOQERI_RELEASE_TARGET or RELEASE_TARGET}"
: "${NOQERI_ARTIFACT_DIR:=artifacts}"
: "${NOQERI_STAGE_DIR:=stage}"
# GitHub workflow_dispatch and one-shot main-branch release runs have
# GITHUB_REF_NAME=main, but artifacts must always identify the compiler's
# actual source version. The final publisher enforces tag/source consistency.
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
version="$(sed -nE 's/^project\(noqeri VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$project_root/CMakeLists.txt")"
test -n "$version" || { echo "Noqeri CMake project version missing" >&2; exit 1; }
ref="v$version"
target="${NOQERI_RELEASE_TARGET}"
name="noqeri-sdk-${ref}-${target}"
mkdir -p "${NOQERI_ARTIFACT_DIR}"
test -d "${NOQERI_STAGE_DIR}/share/noqeri/Lib"
test -d "${NOQERI_STAGE_DIR}/include/noqeri"
# All five SDK pieces must be installed, not merely the compiler.
for component in \
  "share/noqeri/Lib/std/string.noqeri" \
  "share/noqeri/Lib/std/file.noqeri" \
  "share/noqeri/Lib/std/http.noqeri" \
  "share/noqeri/SDK.md"; do
  test -s "${NOQERI_STAGE_DIR}/${component}" || {
    echo "Missing SDK component: ${component}" >&2; exit 1;
  }
done
case "${target}" in
  windows-*)
    test -s "${NOQERI_STAGE_DIR}/bin/noqeri-web.exe"
    test -s "${NOQERI_STAGE_DIR}/bin/noqeri.exe"
    test -s "${NOQERI_STAGE_DIR}/bin/noqeri-nqri.exe"
    test -s "${NOQERI_STAGE_DIR}/bin/noqeri-pkg.exe"
    cmake -E tar cf "${NOQERI_ARTIFACT_DIR}/${name}.zip" --format=zip -- "${NOQERI_STAGE_DIR}"
    ;;
  *)
    test -s "${NOQERI_STAGE_DIR}/bin/noqeri-web"
    test -s "${NOQERI_STAGE_DIR}/bin/noqeri"
    test -s "${NOQERI_STAGE_DIR}/bin/noqeri-nqri"
    test -s "${NOQERI_STAGE_DIR}/bin/noqeri-pkg"
    cmake -E tar czf "${NOQERI_ARTIFACT_DIR}/${name}.tar.gz" -- "${NOQERI_STAGE_DIR}"
    ;;
esac
echo "Packaged complete Noqeri SDK archive for ${target}"
