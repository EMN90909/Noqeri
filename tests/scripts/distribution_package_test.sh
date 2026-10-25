#!/usr/bin/env bash
# Isolated deterministic installer smoke test: no real compiler build needed.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
test -f "$root/cmake/NoqeriPackaging.cmake"
bash -n "$root/scripts/distribution/archive-sdk.sh"
bash -n "$root/scripts/distribution/termux-deb.sh"
sh -n "$root/scripts/distribution/openbsd-package.sh"
version="$(sed -nE 's/^project\(noqeri VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$root/CMakeLists.txt")"
test -n "$version"
mkdir -p "$tmp/stage/bin" "$tmp/stage/include/noqeri" "$tmp/stage/share/noqeri/Lib/std" "$tmp/artifacts"
printf '#!/bin/sh\necho noqeri-fixture\n' > "$tmp/stage/bin/noqeri"
cp "$tmp/stage/bin/noqeri" "$tmp/stage/bin/noqeri-nqri"
chmod 755 "$tmp/stage/bin/noqeri" "$tmp/stage/bin/noqeri-nqri"
printf 'fixture\n' > "$tmp/stage/share/noqeri/Lib/std/render2d.noqeri"
printf '// fixture\n' > "$tmp/stage/include/noqeri/abi.h"
(
  cd "$tmp"
  export NOQERI_RELEASE_TARGET=linux-x86 GITHUB_REF_NAME="main"
  bash "$root/scripts/distribution/archive-sdk.sh"
  test -s "artifacts/noqeri-v$version-linux-x86.tar.gz"
  test -s "artifacts/noqeri-v$version-linux-x86.bin"
  test -s "artifacts/noqeri-nqri-v$version-linux-x86.bin"
  tar -tzf "artifacts/noqeri-v$version-linux-x86.tar.gz" | grep -F 'stage/bin/noqeri'
  bash "$root/scripts/distribution/termux-deb.sh" stage artifacts android-armv7 "$version"
  test "$(dpkg-deb --field "artifacts/noqeri-$version-android-armv7-termux.deb" Architecture)" = arm
  dpkg-deb --contents "artifacts/noqeri-$version-android-armv7-termux.deb" |
    grep -F 'data/data/com.termux/files/usr/bin/noqeri'
  cp stage/bin/noqeri stage/bin/noqeri.exe
  cp stage/bin/noqeri-nqri stage/bin/noqeri-nqri.exe
  export NOQERI_RELEASE_TARGET=windows-x86
  bash "$root/scripts/distribution/archive-sdk.sh"
  test -s "artifacts/noqeri-v$version-windows-x86.zip"
  test -s "artifacts/noqeri-v$version-windows-x86.exe"
  unzip -l "artifacts/noqeri-v$version-windows-x86.zip" | grep -F 'stage/bin/noqeri.exe'
)
# Verify CPack reads the production packaging module, emits a native DEB,
# chooses amd64 metadata and uses relative system paths from install().
mkdir -p "$tmp/fixture" "$tmp/installers"
cp "$root/LICENSE" "$tmp/fixture/LICENSE"
cat > "$tmp/fixture/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.16)
project(noqeri VERSION 0.1.3 LANGUAGES CXX)
include(GNUInstallDirs)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/noqeri" "#!/bin/sh\nexit 0\n")
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/noqeri" DESTINATION bin)
set(NOQERI_PACKAGE_TARGET linux-x86_64)
include("$ENV{NOQERI_PROJECT_ROOT}/cmake/NoqeriPackaging.cmake")
CMAKE
export NOQERI_PROJECT_ROOT="$root"
cmake -S "$tmp/fixture" -B "$tmp/cpack-build"
cpack --config "$tmp/cpack-build/CPackConfig.cmake" -G DEB -B "$tmp/installers"
deb="$tmp/installers/noqeri-0.1.3-linux-x86_64.deb"
test -s "$deb"
test "$(dpkg-deb --field "$deb" Architecture)" = amd64
dpkg-deb --contents "$deb" | grep -F 'usr/bin/noqeri'
echo "Noqeri archive, binary, Termux and CPack DEB packaging smoke tests passed"
