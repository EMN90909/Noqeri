$ErrorActionPreference = 'Stop'
$required = @(
  'README.md','LICENSE','LEGAL.md','SECURITY.md','CONTRIBUTING.md','CMakeLists.txt','project.nqon','noqeri.lock','bootstrap.ps1','bootstrap.sh',
  'Brand/noqeri-logo.webp','Brand/noqeri-mark.png','Brand/noqeri-banner.txt','Grammar/noqeri.grammar.md','Include/noqeri/noqeri.hpp',
  'Compiler/core.cpp','Compiler/semantic_rules.noqeri','Compiler/bootstrap/lexer_host.cpp','Compiler/bootstrap/parser_host.cpp','Compiler/bootstrap/abi_runtime_host.cpp','Compiler/bootstrap/interpreter_host.cpp','Compiler/bootstrap/cli_host.cpp','Compiler/bootstrap/formatter_host.cpp','Compiler/bootstrap/lsp_host.cpp','Compiler/bootstrap/package_host.cpp','Compiler/bootstrap/test_runner_host.cpp','Compiler/bootstrap/production_doctor_host.cpp','Compiler/bootstrap/noqeridb_host.cpp','Compiler/bootstrap/security_audit_host.cpp',
  'Parser/ascii.noqeri','Parser/token_rules.noqeri','Runtime/status.noqeri','Runtime/memory.noqeri','Runtime/limits.noqeri','Programs/selftest.noqeri','Programs/diagnostics.noqeri','Programs/version.noqeri',
  'Modules/builtin.noqeri','Modules/native.noqeri','Objects/value.noqeri','Objects/text.noqeri','Database/noqeridb.noqeri',
  'Tools/security/policy.noqeri','Tools/fuzz/generator.noqeri','Tools/build/policy.noqeri','Tools/scripts/source_floor.noqeri',
  'Lib/security/memory.noqeri','Lib/test/assert.noqeri','Lib/std/int.noqeri','Lib/std/slice_i64.noqeri','Lib/std/search.noqeri','Lib/std/sort.noqeri','Lib/std/stats.noqeri','Lib/std/bytes.noqeri','Lib/std/utf8.noqeri','Lib/std/checksum.noqeri','Lib/std/memory.noqeri','Lib/std/status.noqeri','Lib/std/time.noqeri','Lib/std/random.noqeri','Lib/std/matrix_i64.noqeri','Lib/std/range.noqeri','Lib/std/text.noqeri','Lib/std/set_i64.noqeri','Lib/std/map_i64.noqeri','Lib/std/stack_i64.noqeri','Lib/std/queue_i64.noqeri','Lib/std/algorithm.noqeri','Lib/std/bit.noqeri','Lib/std/validation.noqeri','Lib/std/window.noqeri','Lib/std/pair_i64.noqeri','Lib/std/counter.noqeri','Lib/std/compare.noqeri',
  'tests/noqeri_owned_components.noqeri','tests/noqeri_library_suite.noqeri','examples/ecosystem_smoke.noqeri','editors/vscode/package.json','site/index.html'
)
foreach ($path in $required) { if (-not (Test-Path $path)) { throw "missing required repository path: $path" } }

$obsolete = @('Modules/README.md','Objects/README.md','Lib/README.md','Lib/test/README.md','Android/README.md','Mac/README.md','PC/README.md','PCbuild/README.md','Platforms/README.md','Parser/README.md','Runtime/README.md','Programs/README.md','Benchmarks/README.md','Compiler/README.md','Tools/README.md','Tools/build/README.md','Tools/fuzz/README.md','Parser/lexer.cpp','Parser/parser.cpp','Runtime/abi.cpp','Runtime/interpreter.cpp','Programs/noqeri.cpp','Programs/formatter.cpp','Programs/lsp.cpp','Programs/package.cpp','Programs/test_runner.cpp','Tools/build/production.cpp')
foreach ($path in $obsolete) { if (Test-Path $path) { throw "obsolete placeholder/bootstrap-leak path present: $path" } }

$legacy = Get-ChildItem -Recurse -File | Where-Object { $_.Extension -eq '.noe' -or $_.Extension -eq '.ric' }
if ($legacy) { throw "legacy source extensions found: $($legacy.FullName -join ', ')" }

$noqeriOwned = @('Modules','Objects','Lib','Database','Parser','Runtime','Programs','Tools/security','Tools/fuzz','Tools/build','Platforms','PC','Mac','Android','PCbuild')
$nativeLibrarySource = Get-ChildItem $noqeriOwned -Recurse -File | Where-Object { $_.Extension -in @('.c','.cc','.cpp','.h','.hpp') }
if ($nativeLibrarySource) {
  throw "C/C++ source found in Noqeri-owned implementation directories: $($nativeLibrarySource.FullName -join ', '). Move bootstrap/host C++ under Compiler/bootstrap and keep product-facing implementation in .noqeri"
}

$noqeriCount = (Get-ChildItem -Recurse -File -Filter '*.noqeri').Count
$stdlibCount = (Get-ChildItem 'Lib/std' -Recurse -File -Filter '*.noqeri').Count
if ($noqeriCount -lt 30) { throw "Noqeri source floor failed: $noqeriCount < 30" }
if ($stdlibCount -lt 16) { throw "Noqeri stdlib floor failed: $stdlibCount < 16" }
Write-Host "repository structure verified: Noqeri source=$noqeriCount stdlib=$stdlibCount; non-compiler C++ excluded from Noqeri-owned areas"
