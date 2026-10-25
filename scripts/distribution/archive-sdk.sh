#!/usr/bin/env bash
# Creates a portable SDK archive and directly downloadable native executables.
# Native installer generation is separate, and always uses the SAME build tree.
set -euo pipefail
NOQERI_RELEASE_TARGET="${NOQERI_RELEASE_TARGET:-${RELEASE_TARGET:-}}"
: "${NOQERI_RELEASE_TARGET:?set NOQERI_RELEASE_TARGET or RELEASE_TARGET}"
: "${NOQERI_ARTIFACT_DIR:=artifacts}"
: "${NOQERI_STAGE_DIR:=stage}"
# GitHub workflow_dispatch and one-shot main-branch release runs have
# GITHUB_REF_NAME=main, but artifacts must always identify the compiler's
# actual source version. The final publisher enforces tag/source consistency.
version="$(sed -nE 's/^project\(noqeri VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt)"
test -n "$version" || { echo "Noqeri CMake project version missing" >&2; exit 1; }
ref="v$version"
target="${NOQERI_RELEASE_TARGET}"
name="noqeri-${ref}-${target}"
mkdir -p "${NOQERI_ARTIFACT_DIR}"
test -d "${NOQERI_STAGE_DIR}/share/noqeri/Lib"
test -d "${NOQERI_STAGE_DIR}/include/noqeri"
case "${target}" in
  windows-*)
    test -s "${NOQERI_STAGE_DIR}/bin/noqeri.exe"
    test -s "${NOQERI_STAGE_DIR}/bin/noqeri-nqri.exe"
    cmake -E tar cf "${NOQERI_ARTIFACT_DIR}/${name}.zip" --format=zip -- "${NOQERI_STAGE_DIR}"
    cp "${NOQERI_STAGE_DIR}/bin/noqeri.exe" "${NOQERI_ARTIFACT_DIR}/${name}.exe"
    cp "${NOQERI_STAGE_DIR}/bin/noqeri-nqri.exe" "${NOQERI_ARTIFACT_DIR}/noqeri-nqri-${ref}-${target}.exe"
    ;;
  *)
    test -s "${NOQERI_STAGE_DIR}/bin/noqeri"
    test -s "${NOQERI_STAGE_DIR}/bin/noqeri-nqri"
    cmake -E tar czf "${NOQERI_ARTIFACT_DIR}/${name}.tar.gz" -- "${NOQERI_STAGE_DIR}"
    # The suffix .bin distinguishes these direct native executable downloads
    # from installers and archives. They are host OS/ABI specific, not universal.
    cp "${NOQERI_STAGE_DIR}/bin/noqeri" "${NOQERI_ARTIFACT_DIR}/${name}.bin"
    cp "${NOQERI_STAGE_DIR}/bin/noqeri-nqri" "${NOQERI_ARTIFACT_DIR}/noqeri-nqri-${ref}-${target}.bin"
    chmod 0755 "${NOQERI_ARTIFACT_DIR}/${name}.bin" "${NOQERI_ARTIFACT_DIR}/noqeri-nqri-${ref}-${target}.bin"
    ;;
esac
echo "Packaged portable SDK and direct native executable assets for ${target}"
