#!/usr/bin/env bash
# Installable with Termux's dpkg/apt, NOT a regular Android APK.
set -euo pipefail
if [[ "$#" -ne 4 ]]; then
  echo "usage: $0 <stage-directory> <artifact-directory> <target> <version>" >&2
  exit 2
fi
stage="$1"
artifacts="$2"
target="$3"
version="$4"
case "$target" in
  android-aarch64)  arch=aarch64 ;;
  android-armv7)    arch=arm ;;
  android-x86)      arch=i686 ;;
  android-x86_64)   arch=x86_64 ;;
  *) echo "unsupported Termux target: $target" >&2; exit 2 ;;
esac
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "invalid package version" >&2; exit 2; }
test -s "$stage/bin/noqeri"
test -s "$stage/bin/noqeri-nqri"
root="$(mktemp -d)"
trap 'rm -rf "$root"' EXIT
prefix="$root/data/data/com.termux/files/usr"
mkdir -p "$prefix" "$root/DEBIAN" "$artifacts"
cp -R "$stage/." "$prefix/"
chmod 755 "$prefix/bin/noqeri" "$prefix/bin/noqeri-nqri"
cat > "$root/DEBIAN/control" <<EOF
Package: noqeri
Version: $version
Architecture: $arch
Maintainer: Noethric contributors
Section: devel
Priority: optional
Homepage: https://github.com/EMN90909/Noqeri
Description: Noqeri compiler, NQRI VM and source SDK for Termux
 Native Android command-line development toolchain, built against Bionic.
EOF
dpkg-deb --build --root-owner-group "$root" "$artifacts/noqeri-$version-$target-termux.deb"
dpkg-deb --info "$artifacts/noqeri-$version-$target-termux.deb" | grep -F "Architecture: $arch"
echo "Created Termux installer for $target"
