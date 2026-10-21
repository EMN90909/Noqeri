$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildDir = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { Join-Path $Root 'build' }
$HostBuild = if ($env:NOQERI_HOST_BUILD) { $env:NOQERI_HOST_BUILD } else { Join-Path $BuildDir 'bootstrap-host' }
$Runtime = $env:NOQERI_NQRI_RUNTIME
$ExternalSeed = -not [string]::IsNullOrEmpty($env:NOQERI_STAGE1_NQRI)
$Seed = if ($ExternalSeed) { $env:NOQERI_STAGE1_NQRI } else { Join-Path $BuildDir 'noqeri-stage1.nqri' }
$SeedOrigin = if ($ExternalSeed) { 'externally-pinned' } else { 'local-cpp-stage0' }
$SeedCompiler = $env:NOQERI_STAGE0_CPP
$SeedOverride = [bool]$env:NOQERI_STAGE1_NQRI
$ExpectedSeedHash = $env:NOQERI_STAGE1_SHA256
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
if ($ExternalSeed) {
    if (-not (Test-Path -LiteralPath $Seed -PathType Leaf)) { throw "Explicit Stage-1 seed not found: $Seed" }
    if ($env:NOQERI_STAGE1_SHA256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'External seed requires NOQERI_STAGE1_SHA256 (64 hex digits).' }
} else {
    # Rebuild rather than trusting stale output.
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
    Write-Host 'Generating local Stage-1 seed candidate with C++ Stage-0...'
    & $SeedCompiler bootstrap-seed (Join-Path $Root 'Compiler\selfhost\main.noqeri') $Seed
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $Repro = Join-Path $BuildDir 'noqeri-stage1-repro.nqri'
    try {
        & $SeedCompiler bootstrap-seed (Join-Path $Root 'Compiler\selfhost\main.noqeri') $Repro
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        if ((Get-FileHash -Algorithm SHA256 $Seed).Hash -ne (Get-FileHash -Algorithm SHA256 $Repro).Hash) {
            throw 'Stage-0 produced non-reproducible Stage-1 seed bytes.'
        }
    } finally {
        Remove-Item -LiteralPath $Repro -ErrorAction SilentlyContinue
    }
}
$SeedHash = (Get-FileHash -Algorithm SHA256 $Seed).Hash.ToLowerInvariant()
if ($env:NOQERI_STAGE1_SHA256) {
    if ($env:NOQERI_STAGE1_SHA256 -notmatch '^[0-9a-fA-F]{64}$' -or
        $SeedHash -ne $env:NOQERI_STAGE1_SHA256.ToLowerInvariant()) {
        throw "Stage-1 seed SHA-256 does not match the supplied pin: $SeedHash"
    }
}
$SeedHash = (Get-FileHash -Algorithm SHA256 $Seed).Hash.ToLowerInvariant()
if ($SeedOverride -and $SeedHash -ne $ExpectedSeedHash.ToLowerInvariant()) {
    throw "External Stage-1 seed checksum mismatch; refusing untrusted seed."
}

& $Runtime --selftest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Runtime has-export $Seed stage2CompileToNqri
if ($LASTEXITCODE -ne 0) { throw "Trusted Stage-1 seed is incompatible: it must export stage2CompileToNqri." }
& $Runtime compile-manifest $Seed $Root $Manifest $Stage2 $Target
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Runtime compile-manifest $Stage2 $Root $Manifest $Stage3 $Target
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$Stage2Hash = (Get-FileHash -Algorithm SHA256 $Stage2).Hash.ToLowerInvariant()
$Stage3Hash = (Get-FileHash -Algorithm SHA256 $Stage3).Hash.ToLowerInvariant()
if ($Stage2Hash -ne $Stage3Hash) {
    throw "Noqeri self-host bootstrap FAILED: Stage-2 and Stage-3 differ ($Stage2Hash != $Stage3Hash)."
}

$proofData = [ordered]@{
    schema = 1
    verified = $true
    runtime = $Runtime
    seed = $Seed
    seed_sha256 = $SeedHash
    seed_origin = $SeedOrigin
    stage1_independently_trusted = $false
    source_manifest_sha256 = (Get-FileHash -Algorithm SHA256 $Manifest).Hash.ToLowerInvariant()
    seed_compiler = $SeedCompiler
    seed_origin = if ($SeedOverride) { "external-pinned" } else { "generated-stage0" }
    seed_compiler_sha256 = if ($SeedCompiler -and (Test-Path $SeedCompiler)) { (Get-FileHash -Algorithm SHA256 $SeedCompiler).Hash.ToLowerInvariant() } else { $null }
    stage2 = $Stage2
    stage3 = $Stage3
    stage2_sha256 = $Stage2Hash
    stage3_sha256 = $Stage3Hash
    target = $Target
    equivalent = $true
}
$proofData | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 $Proof
Write-Host 'Native Stage-2/Stage-3 equivalence verified; independent Stage-1 trust NOT attested.'
Write-Host "Stage 2 and Stage 3 are byte-identical: $Stage2Hash"
Write-Host "Proof: $Proof"
