$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildDir = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { Join-Path $Root 'build' }
$HostBuild = if ($env:NOQERI_HOST_BUILD) { $env:NOQERI_HOST_BUILD } else { Join-Path $BuildDir 'bootstrap-host' }
$Runtime = $env:NOQERI_NQRI_RUNTIME
$PinnedSeed = Join-Path $Root 'Compiler\selfhost\trusted-stage1.nqri'
$SeedLock = Join-Path $Root 'Compiler\selfhost\trusted-stage1.sha256'
$Seed = if ($env:NOQERI_STAGE1_NQRI) { $env:NOQERI_STAGE1_NQRI } elseif (Test-Path $PinnedSeed) { $PinnedSeed } else { Join-Path $BuildDir 'noqeri-stage1.nqri' }
$SeedCompiler = $env:NOQERI_STAGE0_CPP
$Manifest = Join-Path $Root 'Compiler\selfhost\stage2-sources.txt'
$Stage2 = Join-Path $BuildDir 'noqeri-stage2.nqri'
$Stage3 = Join-Path $BuildDir 'noqeri-stage3.nqri'
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
        (Join-Path $BuildDir 'noqeri-nqri.exe'),
        (Join-Path $HostBuild 'noqeri-nqri.exe'),
        (Join-Path $HostBuild 'Release\noqeri-nqri.exe'),
        (Join-Path $BuildDir 'noqeri-nqri'),
        (Join-Path $HostBuild 'noqeri-nqri'),
        (Join-Path $HostBuild 'Release\noqeri-nqri')
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { $Runtime = $candidate; break }
    }
}
if (-not $Runtime) {
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw 'cmake is required once to build the native NQRI bootstrap host, or set NOQERI_NQRI_RUNTIME.'
    }
    & cmake -S $Root -B $HostBuild -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cmake --build $HostBuild --target noqeri-nqri noqeri --config Release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    foreach ($candidate in @(
        (Join-Path $HostBuild 'noqeri-nqri.exe'),
        (Join-Path $HostBuild 'Release\noqeri-nqri.exe'),
        (Join-Path $HostBuild 'noqeri-nqri'),
        (Join-Path $HostBuild 'Release\noqeri-nqri')
    )) {
        if (Test-Path $candidate) { $Runtime = $candidate; break }
    }
}
if (-not $Runtime -or -not (Test-Path $Runtime)) {
    throw 'Native NQRI runtime was not produced. Set NOQERI_NQRI_RUNTIME to noqeri-nqri.'
}
if (-not (Test-Path $Seed)) {
    if (-not $SeedCompiler) {
        foreach ($candidate in @(
            (Join-Path $HostBuild 'noqeri.exe'),
            (Join-Path $HostBuild 'Release\noqeri.exe'),
            (Join-Path $HostBuild 'noqeri'),
            (Join-Path $HostBuild 'Release\noqeri')
        )) {
            if (Test-Path $candidate) { $SeedCompiler = $candidate; break }
        }
    }
    if (-not $SeedCompiler -or -not (Test-Path $SeedCompiler)) {
        if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
            throw 'cmake is required to build the trusted C++ Stage-0 seed compiler.'
        }
        & cmake -S $Root -B $HostBuild -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & cmake --build $HostBuild --target noqeri noqeri-nqri --config Release
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        foreach ($candidate in @(
            (Join-Path $HostBuild 'noqeri.exe'),
            (Join-Path $HostBuild 'Release\noqeri.exe'),
            (Join-Path $HostBuild 'noqeri'),
            (Join-Path $HostBuild 'Release\noqeri')
        )) {
            if (Test-Path $candidate) { $SeedCompiler = $candidate; break }
        }
    }
    if (-not $SeedCompiler -or -not (Test-Path $SeedCompiler)) {
        throw 'Trusted C++ Stage-0 compiler was not produced. Set NOQERI_STAGE0_CPP.'
    }
    Write-Host 'Generating trusted Stage-1 NQRI seed with C++ Stage-0 compiler...'
    & $SeedCompiler bootstrap-seed (Join-Path $Root 'Compiler\selfhost\main.noqeri') $Seed
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

& $Runtime --selftest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
# A seed is trusted only if it matches the committed binary and SHA-256 lock.
if ((Test-Path $PinnedSeed) -or (Test-Path $SeedLock)) {
    if (-not (Test-Path $PinnedSeed) -or -not (Test-Path $SeedLock)) {
        throw 'Incomplete trusted Stage-1 seed pin: both module and checksum are required.'
    }
    $ExpectedSeedHash = ((Get-Content $SeedLock -TotalCount 1) -split '\s+')[0].ToLowerInvariant()
    if ($ExpectedSeedHash -cnotmatch '^[0-9a-f]{64}
if ($LASTEXITCODE -ne 0) { throw "Trusted Stage-1 seed is incompatible: it must export stage2CompileToNqri." }
& $Runtime compile-manifest $Seed $Root $Manifest $Stage2 $Target
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Runtime syntax $Stage2 (Join-Path $Root "tests\bootstrap_language_compat.noqeri")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Runtime check $Stage2 (Join-Path $Root "tests\bootstrap_language_compat.noqeri") $Target
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
    seed_compiler = $SeedCompiler
    seed_compiler_sha256 = if ($SeedCompiler -and (Test-Path $SeedCompiler)) { (Get-FileHash -Algorithm SHA256 $SeedCompiler).Hash.ToLowerInvariant() } else { $null }
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
) { throw 'Invalid trusted Stage-1 SHA-256 lock.' }
    $PinnedSeedHash = (Get-FileHash -Algorithm SHA256 $PinnedSeed).Hash.ToLowerInvariant()
    $SeedHashCheck = (Get-FileHash -Algorithm SHA256 $Seed).Hash.ToLowerInvariant()
    if ($PinnedSeedHash -cne $ExpectedSeedHash -or $SeedHashCheck -cne $ExpectedSeedHash) {
        throw 'Stage-1 seed hash mismatch; refusing to run an unpinned compiler.'
    }
    Write-Host "Verified pinned Stage-1 NQRI seed SHA-256: $SeedHashCheck"
}
& $Runtime has-export $Seed stage2CompileToNqri
if ($LASTEXITCODE -ne 0) { throw "Trusted Stage-1 seed is incompatible: it must export stage2CompileToNqri." }
& $Runtime compile-manifest $Seed $Root $Manifest $Stage2 $Target
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Runtime syntax $Stage2 (Join-Path $Root "tests\bootstrap_language_compat.noqeri")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Runtime check $Stage2 (Join-Path $Root "tests\bootstrap_language_compat.noqeri") $Target
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
    seed_compiler = $SeedCompiler
    seed_compiler_sha256 = if ($SeedCompiler -and (Test-Path $SeedCompiler)) { (Get-FileHash -Algorithm SHA256 $SeedCompiler).Hash.ToLowerInvariant() } else { $null }
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
