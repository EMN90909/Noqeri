$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildDir = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { Join-Path $Root 'build' }
$HostBuild = if ($env:NOQERI_HOST_BUILD) { $env:NOQERI_HOST_BUILD } else { Join-Path $BuildDir 'bootstrap-host' }
$Runtime = $env:NOQERI_NQO_RUNTIME
$Seed = if ($env:NOQERI_STAGE1_NQO) { $env:NOQERI_STAGE1_NQO } else { Join-Path $BuildDir 'noqeri-stage1.nqo' }
$Manifest = Join-Path $Root 'Compiler\selfhost\stage2-sources.txt'
$Stage2 = Join-Path $BuildDir 'noqeri-stage2.nqo'
$Stage3 = Join-Path $BuildDir 'noqeri-stage3.nqo'
$Proof = Join-Path $BuildDir 'native-selfhost-proof.json'

function Resolve-HostTarget {
    if ($env:NOQERI_TARGET) { return [int]$env:NOQERI_TARGET }
    $arch = [System.Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString()
    if ($IsWindows -or $env:OS -eq 'Windows_NT') {
        if ($arch -eq 'X64') { return 1 }
        if ($arch -eq 'Arm64') { return 2 }
    }
    if ($IsLinux) {
        if ($arch -eq 'X64') { return 3 }
        if ($arch -eq 'Arm64') { return 4 }
    }
    if ($IsMacOS) {
        if ($arch -eq 'X64') { return 5 }
        if ($arch -eq 'Arm64') { return 6 }
    }
    throw "Unsupported host architecture $arch. Set NOQERI_TARGET explicitly."
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$Target = Resolve-HostTarget

if (-not $Runtime) {
    $candidates = @(
        (Join-Path $BuildDir 'noqeri-nqo.exe'),
        (Join-Path $HostBuild 'noqeri-nqo.exe'),
        (Join-Path $HostBuild 'Release\noqeri-nqo.exe'),
        (Join-Path $BuildDir 'noqeri-nqo'),
        (Join-Path $HostBuild 'noqeri-nqo'),
        (Join-Path $HostBuild 'Release\noqeri-nqo')
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { $Runtime = $candidate; break }
    }
}
if (-not $Runtime) {
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw 'cmake is required once to build the native NQO2 bootstrap host, or set NOQERI_NQO_RUNTIME.'
    }
    & cmake -S $Root -B $HostBuild -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cmake --build $HostBuild --target noqeri-nqo --config Release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    foreach ($candidate in @(
        (Join-Path $HostBuild 'noqeri-nqo.exe'),
        (Join-Path $HostBuild 'Release\noqeri-nqo.exe'),
        (Join-Path $HostBuild 'noqeri-nqo'),
        (Join-Path $HostBuild 'Release\noqeri-nqo')
    )) {
        if (Test-Path $candidate) { $Runtime = $candidate; break }
    }
}
if (-not $Runtime -or -not (Test-Path $Runtime)) {
    throw 'Native NQO2 runtime was not produced. Set NOQERI_NQO_RUNTIME to noqeri-nqo.'
}
if (-not (Test-Path $Seed)) {
    throw 'A trusted Stage-1 NQO2 compiler seed is required. Set NOQERI_STAGE1_NQO. After that trust boundary, Stage 1 builds Stage 2 and Stage 2 rebuilds Stage 3 using the native NQO2 host.'
}

& $Runtime --selftest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Runtime compile-manifest $Seed $Root $Manifest $Stage2 $Target
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Runtime compile-manifest $Stage2 $Root $Manifest $Stage3 $Target
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$Stage2Hash = (Get-FileHash -Algorithm SHA256 $Stage2).Hash.ToLowerInvariant()
$Stage3Hash = (Get-FileHash -Algorithm SHA256 $Stage3).Hash.ToLowerInvariant()
if ($Stage2Hash -ne $Stage3Hash) {
    throw "Noqeri self-host bootstrap FAILED: Stage-2 and Stage-3 differ ($Stage2Hash != $Stage3Hash)."
}

$SeedHash = (Get-FileHash -Algorithm SHA256 $Seed).Hash.ToLowerInvariant()
$proofData = [ordered]@{
    schema = 1
    verified = $true
    runtime = $Runtime
    seed = $Seed
    seed_sha256 = $SeedHash
    stage2 = $Stage2
    stage3 = $Stage3
    stage2_sha256 = $Stage2Hash
    stage3_sha256 = $Stage3Hash
    target = $Target
    equivalent = $true
}
$proofData | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 $Proof
Write-Host 'Noqeri native self-host bootstrap verified.'
Write-Host "Stage 2 and Stage 3 are byte-identical: $Stage2Hash"
Write-Host "Proof: $Proof"
