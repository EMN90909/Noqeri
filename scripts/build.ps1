$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildDir = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { Join-Path $Root 'build' }
$Stage0 = $env:NOQERI_STAGE0
$Compiler = Join-Path $Root 'Compiler\selfhost\main.nqr'
if (-not $Stage0) {
  $found = Get-Command noqeri -ErrorAction SilentlyContinue
  if ($found) { $Stage0 = $found.Source }
}
if (-not $Stage0) {
  Write-Error "Noqeri's compiler is written in Noqeri. Set NOQERI_STAGE0 to a trusted Noqeri seed. The normal main-branch build does not use CMake or C++."
}
if (-not (Get-Command node -ErrorAction SilentlyContinue)) { Write-Error 'The portable Stage-2 host currently requires Node.js 20+' }
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
& $Stage0 check $Compiler
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$Module = Join-Path $BuildDir 'noqeri-stage2.mjs'
& $Stage0 web $Compiler $Module
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& node --check $Module
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$env:NOQERI_STAGE2_MODULE = $Module
& node (Join-Path $Root 'scripts\\verify-stage2-module.mjs')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$Launcher = Join-Path $BuildDir 'noqeri.cmd'
$Cli = Join-Path $Root 'Runtime\stage2-cli.mjs'
Set-Content -Path $Launcher -Encoding ascii -Value "@echo off`r`nset NOQERI_STAGE2_MODULE=$Module`r`nnode `"$Cli`" %*`r`n"
& $Launcher selftest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Launcher stage2-status
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Launcher --version
Write-Host "Noqeri Stage-2 pipeline artifact built from Noqeri compiler source at $Launcher"
Write-Host 'Bootstrap equivalence remains governed separately by scripts/bootstrap-stage2.mjs.'
