#!/usr/bin/env node
import { readFile } from 'node:fs/promises'
import { existsSync } from 'node:fs'
import { pathToFileURL } from 'node:url'
import { resolve } from 'node:path'

function usage() {
  console.log(`Noqeri Stage-2 portable compiler

Usage:
  noqeri --version
  noqeri web-project [source-dir] [output-dir] (HTML/CSS/JS assets only)\n  noqeri web <file.nqr> <output.mjs> (restricted browser subset)\n  noqeri check <file.nqr>
  noqeri syntax <file.nqr>
  noqeri lex-count <file.nqr>
  noqeri fingerprint <file.nqr>
  noqeri stage2-status [--json]
  noqeri add <namespace/name>[@version]
  noqeri install
  noqeri resolve <file.nqr>
  noqeri vendor [--offline]
  noqeri audit [--offline]
  noqeri env [--json]
  noqeri doctor [--json] [--strict]
  noqeri selftest

'check' is a Stage-2 semantic/compile-readiness check for the current host target.
The CLI fails closed if the loaded compiler module does not expose the Stage-2 pipeline.`)
}

const args = process.argv.slice(2)
const command = args[0] || '--help'
if (command === 'noe') {
  // Transitional reference interpreter for .noe scripts and .nqd snapshots.
  // No native .nqr compiler-runtime equivalence is implied.
  const {spawnSync} = await import('node:child_process')
  const noesqlCli = new URL('./noesql/cli.mjs', import.meta.url)
  const child = spawnSync(process.execPath, [fileURLToPath(noesqlCli), ...args.slice(1)], {
    stdio: 'inherit', timeout: 60000
  })
  if (child.error) console.error('NoeSQL reference host failed: '+child.error.message)
  process.exit(child.status ?? 1)
}

if (command === 'sql') {
  // The main compiler remains self-hosted. Database adapters are native
  // optional host capabilities and must not be mistaken for compiler output.
  if (process.env.NOQERI_SQL_DELEGATED === '1') {
    console.error('NoqeriSQL adapter recursion detected')
    process.exit(2)
  }
  const adapter = process.env.NOQERI_SQL_ADAPTER
  if (!adapter) {
    console.error('NoqeriSQL requires a native adapter; set NOQERI_SQL_ADAPTER to the built noqerisql executable.')
    console.error('Build with CMake target noqerisql; SQLite3/libpq development libraries enable each backend.')
    process.exit(2)
  }
  const { spawnSync } = await import('node:child_process')
  const child = spawnSync(adapter, ['sql', ...args.slice(1)], {
    stdio: 'inherit',
    timeout: 60000,
    env: { ...process.env, NOQERI_SQL_DELEGATED: '1' }
  })
  if (child.error) console.error('NoqeriSQL adapter failed: ' + child.error.message)
  process.exit(child.status ?? 1)
}

let packageManager = null
async function packages() {
  if (!packageManager) {
    const { createPackageManager } = await import('./package-manager.mjs')
    packageManager = createPackageManager()
  }
  return packageManager
}

function hostTarget() {
  if (process.platform === 'win32' && process.arch === 'x64') return 1
  if (process.platform === 'win32' && process.arch === 'arm64') return 2
  if (process.platform === 'linux' && process.arch === 'x64') return 3
  if (process.platform === 'linux' && process.arch === 'arm64') return 4
  if (process.platform === 'darwin' && process.arch === 'x64') return 5
  if (process.platform === 'darwin' && process.arch === 'arm64') return 6
  return 0
}

function modulePath() {
  return process.env.NOQERI_STAGE2_MODULE || resolve(process.cwd(), 'build/noqeri-stage2.mjs')
}

function environmentSnapshot() {
  const path = modulePath()
  const major = Number(process.versions.node.split('.')[0] || 0)
  return {
    node: process.version,
    nodeSupported: major >= 20,
    platform: process.platform,
    arch: process.arch,
    target: hostTarget(),
    cwd: process.cwd(),
    compilerModule: path,
    compilerModuleExists: existsSync(path),
    projectManifestExists: existsSync(resolve(process.cwd(), 'project.nqr')),
    lockfileExists: existsSync(resolve(process.cwd(), 'noqeri.lock')),
    offline: process.env.NOQERI_OFFLINE === '1',
    home: process.env.NOQERI_HOME || null,
    registryIndex: process.env.NOQERI_REGISTRY_INDEX || 'https://raw.githubusercontent.com/EMN90909/noqeri-registry/main/registry/index.json'
  }
}

function printEnvironment(snapshot, json) {
  if (json) { console.log(JSON.stringify(snapshot, null, 2)); return }
  console.log(`Node: ${snapshot.node} (${snapshot.nodeSupported ? 'supported' : 'requires 20+'})`)
  console.log(`Host: ${snapshot.platform}/${snapshot.arch} (Noqeri target ${snapshot.target || 'unsupported'})`)
  console.log(`Working directory: ${snapshot.cwd}`)
  console.log(`Compiler module: ${snapshot.compilerModule} (${snapshot.compilerModuleExists ? 'present' : 'missing'})`)
  console.log(`project.nqr: ${snapshot.projectManifestExists ? 'present' : 'not found'}`)
  console.log(`noqeri.lock: ${snapshot.lockfileExists ? 'present' : 'not found'}`)
  console.log(`Offline: ${snapshot.offline ? 'yes' : 'no'}`)
  console.log(`NOQERI_HOME: ${snapshot.home || '(default)'}`)
  console.log(`Registry: ${snapshot.registryIndex}`)
}

let compilerCache = null
async function compilerModule() {
  if (compilerCache) return compilerCache
  const path = modulePath()
  let compiler
  try { compiler = await import(pathToFileURL(path).href) }
  catch (error) { throw new Error(`cannot load Stage-2 module ${path}: ${error.message}`) }
  const required = ['stage2SyntaxCheck', 'stage2SemanticCheckMerged', 'stage2CompileReady', 'stage2CompileCheck']
  const missing = required.filter(name => typeof compiler[name] !== 'function')
  if (missing.length) throw new Error(`compiler module is not Stage-2 capable; missing exports: ${missing.join(', ')}`)
  compilerCache = compiler
  return compilerCache
}

async function checkBytes(compiler, bytes, label) {
  const target = hostTarget()
  if (!target) throw new Error(`unsupported host target: ${process.platform}/${process.arch}`)
  const status = Number(compiler.stage2CompileCheck(bytes, target, false))
  if (status !== 0) throw new Error(`${label}: Stage-2 check failed (${status})`)
  return status
}

async function resolveAndCheckPackages(sourcePath, compiler) {
  const resolved = await (await packages()).resolvePackageImports(sourcePath)
  for (const item of resolved) {
    const bytes = new Uint8Array(await readFile(item.entry))
    await checkBytes(compiler, bytes, `${item.coordinate}@${item.version}`)
  }
  return resolved
}

try {
  if (command === '--help' || command === 'help') { usage(); process.exit(0) }
  if (command === 'env') { printEnvironment(environmentSnapshot(), args.includes('--json')); process.exit(0) }
  if (command === 'doctor') {
    const snapshot = environmentSnapshot(), problems = [], warnings = []
    if (!snapshot.nodeSupported) problems.push('Node.js 20+ is required by the current portable Stage-2 host')
    if (!snapshot.target) problems.push(`Noqeri Stage-2 has no host target mapping for ${snapshot.platform}/${snapshot.arch}`)
    if (!snapshot.compilerModuleExists) warnings.push('Stage-2 compiler module is not built; run scripts/build.sh with a trusted NOQERI_STAGE0 seed')
    if (!snapshot.projectManifestExists) warnings.push('project.nqr was not found in the current directory')
    if (snapshot.offline && !snapshot.lockfileExists) warnings.push('offline mode is enabled but noqeri.lock is missing')
    if (snapshot.compilerModuleExists) {
      try { await compilerModule() }
      catch (error) { problems.push(error.message) }
    }
    const report = { status: problems.length ? 'error' : warnings.length ? 'warning' : 'ok', problems, warnings, environment: snapshot }
    if (args.includes('--json')) console.log(JSON.stringify(report, null, 2))
    else {
      printEnvironment(snapshot, false)
      for (const message of problems) console.error(`ERROR: ${message}`)
      for (const message of warnings) console.log(`WARN: ${message}`)
      if (!problems.length && !warnings.length) console.log('Noqeri doctor: Stage-2 environment looks healthy')
    }
    process.exit(problems.length || (args.includes('--strict') && warnings.length) ? 1 : 0)
  }
  if (command === 'web-project') {
    const { buildWebProject } = await import('./web-project.mjs')
    const result = await buildWebProject({ source: args[1] || 'web', out: args[2] || 'dist' })
    console.log('Web assets copied: ' + result.files + ' files (' + result.html + ' HTML); Noqeri source compilation not performed')
    process.exit(0)
  }
  if (command === 'add') { if (!args[1]) throw new Error('noqeri add needs namespace/name[@version]'); await (await packages()).addPackage(args[1]); process.exit(0) }
  if (command === 'install') { await (await packages()).installProject(); process.exit(0) }
  if (command === 'resolve') { if (!args[1]) throw new Error('noqeri resolve needs a source file'); for (const item of await (await packages()).resolvePackageImports(args[1])) console.log(`${item.coordinate}@${item.version} -> ${item.entry} ${item.integrity}`); process.exit(0) }
  if (command === 'vendor') { const { vendorProject } = await import('./ecosystem-tools.mjs'); const result = await vendorProject({ projectDir: process.cwd(), offline: args.includes('--offline') || process.env.NOQERI_OFFLINE === '1' }); console.log(`vendored ${result.packages.length} locked package(s) into ${result.vendorHome}`); console.log('offline: set NOQERI_HOME=.noqeri/vendor-home and NOQERI_OFFLINE=1'); process.exit(0) }
  if (command === 'audit') { const { auditProject } = await import('./ecosystem-tools.mjs'); const result = await auditProject({ projectDir: process.cwd(), offline: args.includes('--offline') || process.env.NOQERI_OFFLINE === '1' }); console.log(JSON.stringify(result.report, null, 2)); process.exit(result.exitCode) }

  const compiler = await compilerModule()
  if (command === '--version') {
    const major = compiler.stage1VersionMajor?.() ?? 1, minor = compiler.stage1VersionMinor?.() ?? 0, patch = compiler.stage1VersionPatch?.() ?? 0
    console.log(`Noqeri ${major}.${minor}.${patch} Stage-2 pipeline`)
    process.exit(0)
  }
  if (command === 'stage2-status') {
    const target = hostTarget()
    const report = {
      module: modulePath(), target,
      syntax: typeof compiler.stage2SyntaxCheck === 'function',
      semantics: typeof compiler.stage2SemanticCheckMerged === 'function',
      compileCheck: typeof compiler.stage2CompileCheck === 'function',
      compileToNir: typeof compiler.stage2CompileToNir === 'function',
      proofGate: typeof compiler.stage2ProofStatus === 'function' && typeof compiler.stage2Verified === 'function'
    }
    if (args.includes('--json')) console.log(JSON.stringify(report, null, 2))
    else console.log(`Stage-2 pipeline: syntax=${report.syntax} semantics=${report.semantics} compile-check=${report.compileCheck} nir=${report.compileToNir} proof-gate=${report.proofGate} target=${target}`)
    process.exit(report.syntax && report.semantics && report.compileCheck && target ? 0 : 1)
  }
  if (command === 'web') {
    if (!args[1] || !args[2]) throw new Error('web requires an input .nqr and output .mjs path')
    if (!args[1].endsWith('.nqr') || !args[2].endsWith('.mjs')) throw new Error('web requires .nqr input and .mjs output')
    await resolveAndCheckPackages(args[1], compiler)
    const bytes = new Uint8Array(await readFile(args[1]))
    await checkBytes(compiler, bytes, args[1])
    const { compileWebFile } = await import('./web-compiler.mjs')
    await compileWebFile(args[1], args[2])
    console.log('Wrote restricted browser ES module to ' + args[2])
    process.exit(0)
  }
  if (command === 'selftest') {
    const sample = new TextEncoder().encode('function main(): int { return 0 }')
    await checkBytes(compiler, sample, 'selftest')
    const coordinate = new TextEncoder().encode('noqeri/supabase')
    if (typeof compiler.stage1PackageCoordinateValid === 'function' && compiler.stage1PackageCoordinateValid(coordinate) !== true) throw new Error('Stage-2 selftest failed package-coordinate policy')
    console.log(`Stage-2 selftest passed for target ${hostTarget()}`)
    process.exit(0)
  }
  if (!['check', 'syntax', 'lex-count', 'fingerprint'].includes(command)) { usage(); process.exit(2) }
  if (!args[1]) throw new Error(`noqeri: ${command} needs a source file`)

  if (command === 'check') await resolveAndCheckPackages(args[1], compiler)
  const source = new Uint8Array(await readFile(args[1]))
  if (command === 'check') { await checkBytes(compiler, source, args[1]); console.log(`${args[1]}: Stage-2 semantic/compile check OK`); process.exit(0) }
  if (command === 'syntax') { const status = Number(compiler.stage2SyntaxCheck(source)); if (status === 0) { console.log(`${args[1]}: syntax OK`); process.exit(0) } console.error(`${args[1]}: syntax failed (${status})`); process.exit(1) }
  if (command === 'lex-count') { if (typeof compiler.stage1TokenCount !== 'function') throw new Error('token-count compatibility export is unavailable'); console.log(String(compiler.stage1TokenCount(source))); process.exit(0) }
  if (typeof compiler.stage1Fingerprint !== 'function') throw new Error('fingerprint compatibility export is unavailable')
  console.log(String(compiler.stage1Fingerprint(source)))
} catch (error) {
  console.error(`noqeri: ${error.message}`)
  process.exit(1)
}
