#!/bin/sh
# Build an actual OpenBSD pkg_add(1) package from an installed portable SDK.
set -eu
if [ "$#" -ne 4 ]; then
  echo "usage: $0 <stage> <artifacts> <target> <version>" >&2
  exit 2
fi
stage="$1"
artifacts="$2"
target="$3"
version="$4"
case "$target" in openbsd-x86_64|openbsd-aarch64) ;; *) exit 2 ;; esac
case "$version" in *[!0-9.]*|'') echo "Invalid version" >&2; exit 2 ;; esac
test -s "$stage/bin/noqeri"
test -s "$stage/bin/noqeri-nqri"
mkdir -p "$artifacts"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/root/usr/local"
cp -R "$stage/." "$work/root/usr/local/"
printf '%s\n' "Noqeri Stage-0 compiler, portable NQRI runtime and source SDK." > "$work/DESCR"
{
  # pkg_create supplies @name from the package output filename.
  # Writing it again in PLIST causes "Duplicate name in plist".
  printf '%s\n' '@cwd /usr/local'
  (cd "$stage" && find . -type f | sed 's#^./##' | LC_ALL=C sort)
} > "$work/PLIST"
# -B prefixes staged paths, -p records the actual installation prefix.
# pkg_create uses the OpenBSD native packaging format (not an ordinary tarball).
pkg_create -B "$work/root" -p /usr/local -f "$work/PLIST" \
  -D 'COMMENT=Noqeri compiler and runtime' \
  -D 'FULLPKGPATH=devel/noqeri' \
  -D 'MAINTAINER=Noethric contributors' \
  -d "$work/DESCR" "$work/noqeri-$version.tgz"
dest="$artifacts/noqeri-$version-$target.pkg.tgz"
mv "$work/noqeri-$version.tgz" "$dest"
# Verify the pkg_add archive's actual payload rather than running pkg_info,
# which can treat unsigned archive paths as installed package names.
test -s "$dest"
tar -tzf "$dest" | grep -E '(^|/)bin/noqeri
echo "Created native OpenBSD pkg_add package: $dest"
 > /dev/null
echo "Created native OpenBSD pkg_add package: $dest"
