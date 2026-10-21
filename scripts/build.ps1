$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot 'bootstrap-nqri.ps1'
& $script
exit $LASTEXITCODE
