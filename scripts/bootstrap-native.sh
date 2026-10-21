#!/usr/bin/env sh
set -eu
echo "bootstrap-native.sh is intentionally disabled: the current self-host artifact is NQRI bytecode executed by noqeri-nqri, not a native self-hosted compiler." >&2
echo "Use scripts/bootstrap-nqri.sh for the verified NQRI bootstrap. Native self-host qualification requires a Noqeri-written machine-code/object emitter and executable Stage-2/Stage-3 proof." >&2
exit 2
