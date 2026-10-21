#!/usr/bin/env node
import { readFile } from 'node:fs/promises'
import { existsSync } from 'node:fs'
import { pathToFileURL } from 'node:url'
import { resolve } from 'node:path'

function usage() {
  console.log(`Noqeri stage-1 portable compiler kernel

Usage:
  noqeri --version
  noqeri check <file.noqeri>
  noqeri lex-count <file.noqeri>
  noqeri fingerprint <file.noqeri>
  noqeri add <namespace/name>[@version]
  noqeri install
  noqeri resolve <file.noqeri>
  noqeri vendor [--offline]
  noqeri audit [--offline]
  noqeri env [--json]
  noqeri doctor [--json] [--strict]
  noqeri selftest

Registry source uses import package "namespace/name". Packages are cached immutably and re-hashed before use.
Vendoring copies the lockfile's verified packages into .noqeri/vendor-home for NOQERI_OFFLINE=1 builds.
Audit returns 2 when advisory data is unavailable, 1 when a locked dependency is vulnerable, and 0 only after a successful advisory check.
Doctor does not require a working compiler module unless --strict is supplied.`)
}

const args = process.argv.slice(2), command = args[0] || '--help'
let packageManager = null
async function packages() {
  if (!packageManager) {
    const { createPackageManager } = await import(
      process.env.NOQERI_REGISTRY_API_URL ? './package-manager-v1.mjs' : './package-manager.mjs'
    )
    packageManager = createPackageManager()
  }
  return packageManager
}

function environmentSnapshot() {
  const modulePath = process.env.NOQERI_STAGE1_MODULE || resolve(process.cwd(), 'build/noqeri-stage1.nqri')
  const major = Number(process.versions.node.split('.')[0] || 0)
  return {
    node: process.version, nodeSupported: major >= 20, platform: process.platform, arch: process.arch, cwd: process.cwd(),
    compilerModule: modulePath, compilerModuleExists: existsSync(modulePath),
    projectManifestExists: existsSync(resolve(process.cwd(), 'project.noqeri')), lockfileExists: existsSync(resolve(process.cwd(), 'noqeri.lock')),
    offline: process.env.NOQERI_OFFLINE === '1', home: process.env.NOQERI_HOME || null,
    registryIndex: process.env.NOQERI_REGISTRY_INDEX || 'https://raw.githubusercontent.com/EMN90909/noqeri-registry/main/registry/index.json'
  }
}
function printEnvironment(snapshot, json) {
  if (json) { console.log(JSON.stringify(snapshot, null, 2)); return }
  console.log(`Node: ${snapshot.node} (${snapshot.nodeSupported ? 'supported' : 'requires 20+'})`)
  console.log(`Host: ${snapshot.platform}/${snapshot.arch}`)
  console.log(`Working directory: ${snapshot.cwd}`)
  console.log(`Compiler module: ${snapshot.compilerModule} (${snapshot.compilerModuleExists ? 'present' : 'missing'})`)
  console.log(`project.noqeri: ${snapshot.projectManifestExists ? 'present' : 'not found'}`)
  console.log(`noqeri.lock: ${snapshot.lockfileExists ? 'present' : 'not found'}`)
  console.log(`Offline: ${snapshot.offline ? 'yes' : 'no'}`)
  console.log(`NOQERI_HOME: ${snapshot.home || '(default)'}`)
  console.log(`Registry: ${snapshot.registryIndex}`)
}

try {
  if (command === '--help' || command === 'help') { usage(); process.exit(0) }
  if (command === 'env') { printEnvironment(environmentSnapshot(), args.includes('--json')); process.exit(0) }
  if (command === 'doctor') {
    const snapshot = environmentSnapshot(), problems = [], warnings = []
    if (!snapshot.nodeSupported) problems.push('Node.js 20+ is required by the portable stage-1 host')
    if (!snapshot.compilerModuleExists) warnings.push('stage-1 compiler module is not built; run scripts/build.sh with a trusted NOQERI_STAGE0 seed')
    if (!snapshot.projectManifestExists) warnings.push('project.noqeri was not found in the current directory')
    if (snapshot.offline && !snapshot.lockfileExists) warnings.push('offline mode is enabled but noqeri.lock is missing')
    const report = { status: problems.length ? 'error' : warnings.length ? 'warning' : 'ok', problems, warnings, environment: snapshot }
    if (args.includes('--json')) console.log(JSON.stringify(report, null, 2))
    else {
      printEnvironment(snapshot, false)
      for (const message of problems) console.error(`ERROR: ${message}`)
      for (const message of warnings) console.log(`WARN: ${message}`)
      if (!problems.length && !warnings.length) console.log('Noqeri doctor: environment looks healthy')
    }
    process.exit(problems.length || (args.includes('--strict') && warnings.length) ? 1 : 0)
  }
  if (command === 'add') { if (!args[1]) throw new Error('noqeri add needs namespace/name[@version]'); await (await packages()).addPackage(args[1]); process.exit(0) }
  if (command === 'install') { await (await packages()).installProject(); process.exit(0) }
  if (command === 'resolve') { if (!args[1]) throw new Error('noqeri resolve needs a source file'); for (const item of await (await packages()).resolvePackageImports(args[1])) console.log(`${item.coordinate}@${item.version} -> ${item.entry} ${item.integrity}`); process.exit(0) }
  if (command === 'vendor') { const { vendorProject } = await import('./ecosystem-tools.mjs'); const result = await vendorProject({ projectDir: process.cwd(), offline: args.includes('--offline') || process.env.NOQERI_OFFLINE === '1' }); console.log(`vendored ${result.packages.length} locked package(s) into ${result.vendorHome}`); console.log('offline: set NOQERI_HOME=.noqeri/vendor-home and NOQERI_OFFLINE=1'); process.exit(0) }
  if (command === 'audit') { const { auditProject } = await import('./ecosystem-tools.mjs'); const result = await auditProject({ projectDir: process.cwd(), offline: args.includes('--offline') || process.env.NOQERI_OFFLINE === '1' }); console.log(JSON.stringify(result.report, null, 2)); process.exit(result.exitCode) }

  const modulePath = process.env.NOQERI_STAGE1_MODULE || resolve(process.cwd(), 'build/noqeri-stage1.nqri')
  let compiler
  try { compiler = await import(pathToFileURL(modulePath).href) } catch (error) { throw new Error(`cannot load stage-1 module ${modulePath}: ${error.message}`) }
  if (command === '--version') { console.log(`Noqeri ${compiler.stage1VersionMajor()}.${compiler.stage1VersionMinor()}.${compiler.stage1VersionPatch()} stage1`); process.exit(0) }
  if (command === 'selftest') { const sample = new TextEncoder().encode('function main(): int { return 0 }'); const status = compiler.stage1CheckSource(sample), tokens = compiler.stage1TokenCount(sample); const pkg = new TextEncoder().encode('noqeri/supabase'); if (status !== 0 || tokens < 6 || compiler.stage1PackageCoordinateValid?.(pkg) !== true) throw new Error(`stage-1 selftest failed: status=${status} tokens=${tokens}`); console.log(`stage-1 selftest passed (${tokens} tokens; package policy OK)`); process.exit(0) }
  if (!['check', 'lex-count', 'fingerprint'].includes(command)) { usage(); process.exit(2) }
  if (!args[1]) throw new Error(`noqeri: ${command} needs a source file`)
  if (command === 'check') { const resolved = await (await packages()).resolvePackageImports(args[1]); for (const item of resolved) { const packageSource = new Uint8Array(await readFile(item.entry)); const packageStatus = Number(compiler.stage1CheckSource(packageSource)); if (packageStatus !== 0) throw new Error(`${item.coordinate}@${item.version}: package entry failed stage-1 check (${packageStatus})`) } }
  const source = new Uint8Array(await readFile(args[1]))
  if (command === 'check') { const status = Number(compiler.stage1CheckSource(source)); if (status === 0) { console.log(`${args[1]}: syntax/token check OK`); process.exit(0) } console.error(`${args[1]}: stage-1 check failed (${status})`); process.exit(1) }
  if (command === 'lex-count') { console.log(String(compiler.stage1TokenCount(source))); process.exit(0) }
  console.log(String(compiler.stage1Fingerprint(source))); process.exit(0)
} catch (error) { console.error(`noqeri: ${error.message}`); process.exit(1) }
