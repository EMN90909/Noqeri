$ErrorActionPreference = 'Stop'
Write-Error 'bootstrap-native.ps1 is intentionally disabled: the current self-host artifact is NQRI bytecode executed by noqeri-nqri, not a native self-hosted compiler. Use scripts\bootstrap-nqri.ps1 for the verified NQRI bootstrap.'
exit 2
