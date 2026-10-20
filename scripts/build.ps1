$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot 'bootstrap-native.ps1'
& $script
exit $LASTEXITCODE
