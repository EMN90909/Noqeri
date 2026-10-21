$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildDir = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { Join-Path $Root 'build' }
$HostBuild = if ($env:NOQERI_HOST_BUILD) { $env:NOQERI_HOST_BUILD } else { Join-Path $BuildDir 'bootstrap-host' }
$Runtime = $env:NOQERI_NQRI_RUNTIME
$ExternalSeed = -not [string]::IsNullOrEmpty($env:NOQERI_STAGE1_NQRI)
$RepoSeed = Join-Path $Root 'Compiler\selfhost\trusted-stage1.nqri'
$RepoPinFile = Join-Path $Root 'Compiler\selfhost\trusted-stage1.sha256'
$CanonicalPinFile = Join-Path $Root 'Compiler\selfhost\stage1-seed.sha256'
$Seed = if ($ExternalSeed) { $env:NOQERI_STAGE1_NQRI } else { $RepoSeed }
$SeedOrigin = if ($ExternalSeed) { 'externally-pinned' } else { 'repository-committed' }
$SeedCompiler = $null
$Provenance = Join-Path $Root 'Compiler\selfhost\stage1-seed.json'
$PinSource = if ($ExternalSeed) { 'environment' } else { $RepoPinFile }
$Manifest = Join-Path $Root 'Compiler\selfhost\stage2-sources.txt'
$Stage2 = Join-Path $BuildDir 'noqeri-stage2.nqri'
$Stage3 = Join-Path $BuildDir 'noqeri-stage3.nqri'
$Proof = Join-Path $BuildDir 'nqri-selfhost-proof.json'

function Get-Sha256([string]$Path) {
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Read-HashPin([string]$Path) {
    $line = Get-Content -LiteralPath $Path | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -First 1
    if (-not $line) { throw "SHA-256 pin file is empty: $Path" }
    $hash = (($line -split '\s+')[0]).ToLowerInvariant()
    if ($hash -notmatch '^[0-9a-f]{64}$') { throw "Invalid SHA-256 pin in $Path" }
    return $hash
}

function Test-FilesByteEqual([string]$Left, [string]$Right) {
    $leftInfo = Get-Item -LiteralPath $Left
    $rightInfo = Get-Item -LiteralPath $Right
    if ($leftInfo.Length -ne $rightInfo.Length) { return $false }

    $leftStream = [System.IO.File]::OpenRead($Left)
    $rightStream = [System.IO.File]::OpenRead($Right)
    try {
        $leftBuffer = New-Object byte[] 65536
        $rightBuffer = New-Object byte[] 65536
        while ($true) {
            $leftRead = $leftStream.Read($leftBuffer, 0, $leftBuffer.Length)
            $rightRead = $rightStream.Read($rightBuffer, 0, $rightBuffer.Length)
            if ($leftRead -ne $rightRead) { return $false }
            if ($leftRead -eq 0) { return $true }
            for ($i = 0; $i -lt $leftRead; $i++) {
                if ($leftBuffer[$i] -ne $rightBuffer[$i]) { return $false }
            }
        }
    } finally {
        $leftStream.Dispose()
        $rightStream.Dispose()
    }
}

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
        throw 'cmake is required once to build the NQRI VM bootstrap host, or set NOQERI_NQRI_RUNTIME.'
    }
    & cmake -S $Root -B $HostBuild -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cmake --build $HostBuild --target noqeri-nqri --config Release
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
if (-not $Runtime -or -not (Test-Path -LiteralPath $Runtime -PathType Leaf)) {
    throw 'NQRI VM runtime was not produced. Set NOQERI_NQRI_RUNTIME to noqeri-nqri.'
}

if ($ExternalSeed) {
    if (-not (Test-Path -LiteralPath $Seed -PathType Leaf)) { throw "Explicit Stage-1 seed not found: $Seed" }
    if ($env:NOQERI_STAGE1_SHA256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw 'External seed requires NOQERI_STAGE1_SHA256 (64 hex digits).'
    }
    $Pin = $env:NOQERI_STAGE1_SHA256.ToLowerInvariant()
} else {
    if (-not (Test-Path -LiteralPath $RepoSeed -PathType Leaf) -or
        -not (Test-Path -LiteralPath $RepoPinFile -PathType Leaf) -or
        -not (Test-Path -LiteralPath $CanonicalPinFile -PathType Leaf) -or
        -not (Test-Path -LiteralPath $Provenance -PathType Leaf)) {
        throw 'Repository Stage-1 seed package is incomplete; require trusted-stage1.nqri, trusted-stage1.sha256, stage1-seed.sha256 and stage1-seed.json.'
    }
    $RepoPin = Read-HashPin $RepoPinFile
    $CanonicalPin = Read-HashPin $CanonicalPinFile
    if ($RepoPin -ne $CanonicalPin) {
        throw "Repository Stage-1 digest and canonical pin disagree: $RepoPin != $CanonicalPin"
    }
    if ($env:NOQERI_STAGE1_SHA256 -and $env:NOQERI_STAGE1_SHA256.ToLowerInvariant() -ne $RepoPin) {
        throw 'NOQERI_STAGE1_SHA256 cannot override the repository pin without NOQERI_STAGE1_NQRI.'
    }
    $Seed = $RepoSeed
    $Pin = $RepoPin
}

$SeedHash = Get-Sha256 $Seed
if ($SeedHash -ne $Pin) {
    throw "Stage-1 seed SHA-256 does not match the required pin: $SeedHash != $Pin"
}
$Stage1PinVerified = $true

# Stage 1 is not executed until its canonical or explicit SHA-256 pin succeeds.
& $Runtime --selftest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Runtime has-export $Seed stage2CompileToNqri
if ($LASTEXITCODE -ne 0) {
    throw 'Hash-pinned Stage-1 seed is incompatible: it must export stage2CompileToNqri.'
}

& $Runtime compile-manifest $Seed $Root $Manifest $Stage2 $Target
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Runtime compile-manifest $Stage2 $Root $Manifest $Stage3 $Target
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$Stage2Hash = Get-Sha256 $Stage2
$Stage3Hash = Get-Sha256 $Stage3
if (-not (Test-FilesByteEqual $Stage2 $Stage3)) {
    throw "Noqeri NQRI self-host bootstrap FAILED: Stage-2 and Stage-3 differ byte-for-byte ($Stage2Hash != $Stage3Hash)."
}

$proofData = [ordered]@{
    schema = 3
    execution_model = 'nqri-vm'
    artifact_format = 'nqri'
    native_codegen = $false
    native_selfhost = $false
    verified = $true
    runtime = $Runtime
    seed = $Seed
    seed_bytes = (Get-Item -LiteralPath $Seed).Length
    seed_sha256 = $SeedHash
    seed_origin = $SeedOrigin
    stage1_pin_verified = $Stage1PinVerified
    stage1_pin_source = $PinSource
    stage1_seed_provenance = $Provenance
    stage1_independently_trusted = $false
    source_manifest_sha256 = Get-Sha256 $Manifest
    runtime_sha256 = Get-Sha256 $Runtime
    seed_compiler = $SeedCompiler
    seed_compiler_sha256 = if ($SeedCompiler -and (Test-Path -LiteralPath $SeedCompiler -PathType Leaf)) { Get-Sha256 $SeedCompiler } else { $null }
    stage2 = $Stage2
    stage3 = $Stage3
    stage2_sha256 = $Stage2Hash
    stage3_sha256 = $Stage3Hash
    stage_bytes = (Get-Item -LiteralPath $Stage2).Length
    target = $Target
    byte_equal = $true
    equivalent = $true
}
$proofData | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 $Proof

Write-Host "Stage-1 SHA-256 pin verified before execution: $SeedHash"
Write-Host "NQRI Stage-2/Stage-3 byte-for-byte equality verified: $Stage2Hash"
Write-Host "Proof: $Proof"
