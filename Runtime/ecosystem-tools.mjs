#!/usr/bin/env node
import { cp, mkdir, readFile, rm, writeFile } from 'node:fs/promises'
import { existsSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { pathToFileURL } from 'node:url'
import { createPackageManager, compareSemver } from './package-manager.mjs'
import { apiBase } from './registry-client.mjs'


function safeCoordinate(value) {
  if (!/^[A-Za-z0-9_-]+\/[A-Za-z0-9_-]+$/.test(value)) throw new Error(`invalid package coordinate in lock file: ${value}`)
  return value
}
function safeVersion(value) {
  if (!/^[0-9A-Za-z][0-9A-Za-z.+-]*$/.test(value)) throw new Error(`invalid package version in lock file: ${value}`)
  return value
}
export function parseLock(text) {
  const lines = String(text).replace(/\r\n/g, '\n').split('\n').filter(Boolean)
  if (lines[0] === 'noqeri-lock 3') {
    const packages = []
    for (const line of lines.slice(1)) {
      if (!line.startsWith('package ')) continue
      const fields = line.split(/\s+/)
      if (fields.length < 6) throw new Error(`malformed noqeri-lock 3 package line: ${line}`)
      const [, alias, coordinate, version, integrity, entry] = fields
      if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(alias)) throw new Error(`invalid package alias in lock file: ${alias}`)
      if (!/^sha256:[0-9a-f]{64}$/i.test(integrity)) throw new Error(`package ${coordinate} has invalid sha256 integrity`)
      packages.push({ alias, coordinate: safeCoordinate(coordinate), version: safeVersion(version), integrity: integrity.toLowerCase(), entry, lockVersion: 3 })
    }
    return { version: 3, packages }
  }
  if (lines[0] === 'noqeri-lock 2') {
    const packages = []
    for (const line of lines.slice(1)) {
      if (!line.startsWith('dependency ')) continue
      const fields = line.split(/\s+/)
      const checksumAt = fields.indexOf('checksum')
      if (fields.length < 8 || checksumAt < 0 || !fields[checksumAt + 1]) throw new Error(`malformed noqeri-lock 2 dependency line: ${line}`)
      const [, alias, coordinate, version] = fields
      const integrity = fields[checksumAt + 1]
      if (!/^sha256:[0-9a-f]{64}$/i.test(integrity)) throw new Error(`package ${coordinate} has invalid sha256 integrity`)
      packages.push({ alias, coordinate: safeCoordinate(coordinate), version: safeVersion(version), integrity: integrity.toLowerCase(), entry: null, lockVersion: 2 })
    }
    return { version: 2, packages }
  }
  throw new Error('unsupported lock file; expected noqeri-lock 2 or noqeri-lock 3')
}
async function readLock(projectDir) {
  const path = join(resolve(projectDir), 'noqeri.lock')
  return { path, ...parseLock(await readFile(path, 'utf8')) }
}
function packageDestination(home, coordinate, version) {
  const [namespace, name] = coordinate.split('/')
  return join(home, 'cache', 'registry-v1', namespace, name, version)
}
export async function vendorProject({ projectDir = process.cwd(), offline = false } = {}) {
  const root = resolve(projectDir)
  const lock = await readLock(root)
  if (lock.version !== 3) throw new Error('stage-1 vendoring currently requires noqeri-lock 3; lock v2 remains audit-compatible but uses a different native cache/checksum contract')
  const vendorHome = join(root, '.noqeri', 'vendor-home')
  const packages = createPackageManager({ offline })
  await rm(vendorHome, { recursive: true, force: true })
  const manifest = []
  for (const item of lock.packages) {
    const pkg = await packages.fetchPackage(item.coordinate, item.version)
    if (pkg.integrity.toLowerCase() !== item.integrity) {
      throw new Error(`lock integrity mismatch for ${item.coordinate}@${item.version}: lock=${item.integrity} cache=${pkg.integrity}`)
    }
    const destination = packageDestination(vendorHome, item.coordinate, item.version)
    await mkdir(dirname(destination), { recursive: true })
    await cp(pkg.path, destination, { recursive: true, force: true })
    manifest.push({ coordinate: item.coordinate, version: item.version, integrity: item.integrity, entry: item.entry || pkg.entry })
  }
  await mkdir(vendorHome, { recursive: true })
  await writeFile(join(vendorHome, 'vendor-manifest.json'), JSON.stringify({
    schema: 1,
    lock_version: lock.version,
    packages: manifest,
    offline_environment: { NOQERI_HOME: '.noqeri/vendor-home', NOQERI_OFFLINE: '1' }
  }, null, 2) + '\n')
  return { vendorHome, packages: manifest }
}
function termMatches(version, term) {
  if (!term || term === '*') return true
  const m = /^(<=|>=|<|>|=)?([0-9][0-9A-Za-z.+-]*)$/.exec(term)
  if (!m) throw new Error(`unsupported advisory version comparator: ${term}`)
  const op = m[1] || '='
  const cmp = compareSemver(version, m[2])
  return op === '=' ? cmp === 0 : op === '<' ? cmp < 0 : op === '<=' ? cmp <= 0 : op === '>' ? cmp > 0 : cmp >= 0
}
export function versionAffected(version, range) {
  if (!range || range === '*') return true
  return String(range).split('||').some(clause => clause.trim().split(/\s+/).filter(Boolean).every(term => termMatches(version, term)))
}
async function loadAdvisories(projectDir, offline) {
  const configured = process.env.NOQERI_ADVISORY_FILE
  const vendored = join(resolve(projectDir), '.noqeri', 'advisories', 'index.json')
  const local = configured ? resolve(configured) : vendored
  if (existsSync(local)) return { source: local, data: JSON.parse(await readFile(local, 'utf8')) }
  if (offline) throw new Error('advisory data unavailable in offline mode; set NOQERI_ADVISORY_FILE or vendor .noqeri/advisories/index.json')
  const url = process.env.NOQERI_ADVISORY_URL || apiBase() + '/v1/advisories'
  const response = await fetch(url, { headers: { accept: 'application/json', 'user-agent': 'noqeri-audit/1' } })
  if (!response.ok) throw new Error(`advisory fetch failed: HTTP ${response.status}`)
  return { source: url, data: await response.json() }
}
export async function auditProject({ projectDir = process.cwd(), offline = process.env.NOQERI_OFFLINE === '1' } = {}) {
  const lock = await readLock(projectDir)
  let advisory
  try { advisory = await loadAdvisories(projectDir, offline) }
  catch (error) {
    return { exitCode: 2, report: { schema: 1, status: 'unknown', error: error.message, lock_version: lock.version, packages_checked: lock.packages.length, vulnerabilities: [] } }
  }
  if (advisory.data?.schema !== 1 || !Array.isArray(advisory.data?.advisories)) {
    return { exitCode: 2, report: { schema: 1, status: 'unknown', error: 'unsupported advisory index schema', advisory_source: advisory.source, vulnerabilities: [] } }
  }
  const vulnerabilities = []
  for (const pkg of lock.packages) {
    for (const item of advisory.data.advisories) {
      if (item.package !== pkg.coordinate) continue
      const affected = Array.isArray(item.affected_versions) ? item.affected_versions.includes(pkg.version) : versionAffected(pkg.version, item.affected || '*')
      if (affected) vulnerabilities.push({
        id: item.id || 'NOQERI-ADVISORY-UNKNOWN', package: pkg.coordinate, version: pkg.version,
        severity: item.severity || 'unknown', summary: item.summary || '', patched_versions: item.patched_versions || []
      })
    }
  }
  return { exitCode: vulnerabilities.length ? 1 : 0, report: {
    schema: 1, status: vulnerabilities.length ? 'vulnerable' : 'clean', advisory_source: advisory.source,
    lock_version: lock.version, packages_checked: lock.packages.length, vulnerabilities
  } }
}

async function main() {
  const args = process.argv.slice(2)
  const command = args[0]
  const projectDir = args.find(x => x.startsWith('--project='))?.slice('--project='.length) || process.cwd()
  const offline = args.includes('--offline') || process.env.NOQERI_OFFLINE === '1'
  if (command === 'vendor') {
    const result = await vendorProject({ projectDir, offline })
    console.log(JSON.stringify({ status: 'ok', vendor_home: result.vendorHome, packages: result.packages }, null, 2))
    return 0
  }
  if (command === 'audit') {
    const result = await auditProject({ projectDir, offline })
    console.log(JSON.stringify(result.report, null, 2))
    return result.exitCode
  }
  console.error('usage: node Runtime/ecosystem-tools.mjs <vendor|audit> [--project=DIR] [--offline]')
  return 2
}
if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  try { process.exitCode = await main() } catch (error) { console.error(`noqeri ecosystem: ${error.message}`); process.exitCode = 2 }
}
