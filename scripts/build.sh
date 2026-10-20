#!/usr/bin/env sh
set -eu
exec "$(dirname "$0")/bootstrap-native.sh" "$@"
