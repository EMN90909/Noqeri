import { createHash } from 'node:crypto'
import { homedir } from 'node:os'
import { readFile, writeFile, mkdir, readdir, stat, rm, rename } from 'node:fs/promises'
import { resolve, join, relative, sep, dirname } from 'node:path'
import { parseNqon, stringifyNqon } from './nqon.mjs'
import { apiBase, decodeArchive, IDENT, VERSION, INTEGRITY } from './registry-client.mjs'

const coordinatePattern = /^[a-z0-9][a-z0-9-]*\/[a-z0-9][a-z0-9-]*$/
const versionPattern = VERSION
const maxFiles = 256
const maxBytes = 8 * 1024 * 1024
const maxIndexBytes = 2 * 1024 * 1024

export function compareText(a, b) { return a < b ? -1 : a > b ? 1 : 0 }

function parseSemver(version) {
  const match = /^(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$/.exec(version)
  if (!match) return null
  return { major: BigInt(match[1]), minor: BigInt(match[2]), patch: BigInt(match[3]), pre: match[4] ? match[4].split('.') : [] }
}

export function compareSemver(left, right) {
  const a = parseSemver(left), b = parseSemver(right)
  if (!a || !b) throw new Error('invalid semantic version')
  for (const field of ['major', 'minor', 'patch']) {
    if (a[field] < b[field]) return -1
    if (a[field] > b[field]) return 1
  }
  if (a.pre.length === 0 && b.pre.length === 0) return 0
  if (a.pre.length === 0) return 1
  if (b.pre.length === 0) return -1
  const size = Math.max(a.pre.length, b.pre.length)
  for (let index = 0; index < size; index++) {
    if (index >= a.pre.length) return -1
    if (index >= b.pre.length) return 1
    const av = a.pre[index], bv = b.pre[index]
    const an = /^\d+$/.test(av), bn = /^\d+$/.test(bv)
    if (an && bn) {
      const ai = BigInt(av), bi = BigInt(bv)
      if (ai < bi) return -1
      if (ai > bi) return 1
      continue
    }
    if (an !== bn) return an ? -1 : 1
    const order = compareText(av, bv)
    if (order !== 0) return order
  }
  return 0
}

export function safeRelative(path) {
  if (!path || path.startsWith('/') || path.startsWith('\\') || path.includes('\\') || path.split('/').some(part => part === '' || part === '.' || part === '..')) {
    throw new Error(`unsafe package path: ${path}`)
  }
  return path
}

export function parseCoordinate(input) {
  const at = input.lastIndexOf('@')
  let coordinate = input, version = ''
  if (at > 0) { coordinate = input.slice(0, at); version = input.slice(at + 1) }
  if (!coordinatePattern.test(coordinate)) throw new Error('package must be namespace/name')
  if (version && !versionPattern.test(version)) throw new Error('invalid semantic version')
  return { coordinate, version }
}

function packageAlias(coordinate) { return coordinate.split('/')[1].replace(/-/g, '_') }

export function createPackageManager(options = {}) {
  const home = resolve(options.home || process.env.NOQERI_HOME || join(homedir(), '.noqeri'))
  const cacheRoot = join(home, 'cache', 'registry-v1')
  const registryUrl = () => apiBase(options.apiBase || process.env.NOQERI_REGISTRY_API_URL)
  const offline = options.offline ?? (process.env.NOQERI_OFFLINE === '1')
  const fetchImpl = options.fetchImpl || globalThis.fetch
  if (!fetchImpl && !offline) throw new Error('fetch capability unavailable')

  async function responseBytes(url, limit) {
    if (offline) throw new Error('offline mode forbids network access')
    const response = await fetchImpl(url, { headers: { Accept: url.endsWith('/download') ? 'application/vnd.noqeri.package+gzip' : 'application/json', 'User-Agent': 'noqeri-stage1' }, redirect: 'error', signal: AbortSignal.timeout(10000) })
    if (!response.ok) throw new Error(`download ${response.status}: ${url}`)
    const declared = Number(response.headers.get('content-length') || 0)
    if (declared > limit) throw new Error('download exceeds size limit')
    const bytes = new Uint8Array(await response.arrayBuffer())
    if (bytes.byteLength > limit) throw new Error('download exceeds size limit')
    return bytes
  }

  async function jsonFrom(url, limit = maxIndexBytes) {
    const bytes = await responseBytes(url, limit)
    return JSON.parse(new TextDecoder().decode(bytes))
  }

  async function registryIndex() { return jsonFrom(registryUrl() + '/v1/index') }

  function chooseRelease(index, coordinate, requested) {
    const [namespace, name] = coordinate.split('/')
    const pkg = index.packages?.find(item => item.namespace === namespace && item.name === name)
    if (!pkg) throw new Error(`package not found: ${coordinate}`)
    const releases = (pkg.versions || []).filter(item => versionPattern.test(item.version) && !item.yanked && (!requested || item.version === requested)).sort((a, b) => compareSemver(b.version, a.version))
    const release = requested ? releases.find(item => item.version === requested) : releases[0]
    if (!release) throw new Error(`no usable release for ${coordinate}${requested ? '@' + requested : ''}`)
    return { pkg, release }
  }

  function packageDigest(files) {
    const hash = createHash('sha256')
    for (const file of [...files].sort((a, b) => compareText(a.name, b.name))) {
      hash.update(file.name); hash.update('\0'); hash.update(file.bytes); hash.update('\0')
    }
    return `sha256:${hash.digest('hex')}`
  }

  async function localPackageFiles(root, directory = root, out = [], state = { files: 0, bytes: 0 }) {
    const entries = await readdir(directory, { withFileTypes: true })
    entries.sort((a, b) => compareText(a.name, b.name))
    for (const entry of entries) {
      if (entry.name === '.noqeri-integrity.json') continue
      const absolute = join(directory, entry.name)
      if (entry.isDirectory()) { await localPackageFiles(root, absolute, out, state); continue }
      if (!entry.isFile()) throw new Error('unsupported cached package entry type')
      if (++state.files > maxFiles) throw new Error('cached package file limit exceeded')
      const bytes = new Uint8Array(await readFile(absolute))
      state.bytes += bytes.byteLength
      if (state.bytes > maxBytes) throw new Error('cached package size limit exceeded')
      const rel = relative(root, absolute).split(sep).join('/')
      safeRelative(rel)
      out.push({ name: rel, bytes })
    }
    return { files: out, state }
  }

  async function verifyCachedPackage(target, expectedCoordinate, expectedVersion) {
    const meta = JSON.parse(await readFile(join(target, '.noqeri-integrity.json'), 'utf8'))
    if (meta.coordinate !== expectedCoordinate || meta.version !== expectedVersion || !INTEGRITY.test(meta.integrity || '')) throw new Error('cached package metadata mismatch')
    const { files, state } = await localPackageFiles(target)
    const integrity = packageDigest(files)
    if (integrity !== meta.integrity) throw new Error(`cached package integrity mismatch: ${expectedCoordinate}@${expectedVersion}`)
    if (state.files !== meta.files || state.bytes !== meta.bytes) throw new Error('cached package metadata counts mismatch')
    const entry = safeRelative(meta.entry)
    const manifestName = meta.manifestName || 'package.noqeri'
    if (!['package.noqeri', 'package.nqr'].includes(manifestName) || !files.some(file => file.name === manifestName)) throw new Error('cached package manifest missing')
    if (!files.some(file => file.name === entry)) throw new Error('cached package entry missing')
    return { ...meta, manifestName, path: target }
  }

  async function atomicPackageWrite(target, files, metadata) {
    const temp = `${target}.tmp-${process.pid}-${Date.now()}`
    await rm(temp, { recursive: true, force: true })
    await mkdir(temp, { recursive: true, mode: 0o700 })
    try {
      for (const file of files) {
        const destination = join(temp, ...safeRelative(file.name).split('/'))
        const rel = relative(temp, destination)
        if (rel === '..' || rel.startsWith('..' + sep)) throw new Error('package path escaped cache')
        await mkdir(dirname(destination), { recursive: true, mode: 0o700 })
        await writeFile(destination, file.bytes, { mode: 0o600 })
      }
      await writeFile(join(temp, '.noqeri-integrity.json'), JSON.stringify(metadata, null, 2) + '\n', { mode: 0o600 })
      await rm(target, { recursive: true, force: true })
      await mkdir(dirname(target), { recursive: true, mode: 0o700 })
      await rename(temp, target)
    } catch (error) {
      await rm(temp, { recursive: true, force: true })
      throw error
    }
  }

  async function fetchPackage(coordinate, requested = '') {
    const [namespace, name] = coordinate.split('/')
    if (requested && offline) {
      const target = join(cacheRoot, namespace, name, requested)
      return verifyCachedPackage(target, coordinate, requested)
    }
    if (offline) {
      const versionsRoot = join(cacheRoot, namespace, name)
      const names = (await readdir(versionsRoot, { withFileTypes: true })).filter(entry => entry.isDirectory() && versionPattern.test(entry.name)).map(entry => entry.name).sort((a, b) => compareSemver(b, a))
      if (names.length === 0) throw new Error(`package is not cached: ${coordinate}`)
      return verifyCachedPackage(join(versionsRoot, names[0]), coordinate, names[0])
    }
    const index = await registryIndex()
    const { release } = chooseRelease(index, coordinate, requested)
    const target = join(cacheRoot, namespace, name, release.version)
    try { return await verifyCachedPackage(target, coordinate, release.version) } catch {}
    const url = registryUrl() + '/v1/packages/' + namespace + '/' + name + '/' + release.version
    const metadata = await jsonFrom(url)
    if (metadata.version !== release.version || metadata.yanked || !INTEGRITY.test(metadata.archiveIntegrity || '') || (release.archiveIntegrity && release.archiveIntegrity !== metadata.archiveIntegrity)) throw new Error('invalid Registry release metadata')
    const archive = await responseBytes(url + '/download', maxBytes)
    const unpacked = decodeArchive(archive, metadata.archiveIntegrity)
    if (metadata.entry !== unpacked.entry) throw new Error('Registry entry mismatch')
    const files = unpacked.files
    const integrity = packageDigest(files)
    const record = {
      coordinate, version: release.version, integrity, archiveIntegrity: metadata.archiveIntegrity,
      entry: unpacked.entry, manifestName: unpacked.manifestName, legacy: unpacked.legacy,
      files: files.length, bytes: files.reduce((n, file) => n + file.bytes.byteLength, 0)
    }
    if (unpacked.legacy) console.warn('NQR-PLEGACY: package.nqr uses legacy .nqr sources; republish using package.noqeri and .noqeri sources')
    await atomicPackageWrite(target, files, record)
    return { ...record, path: target }
  }

  async function findProject(start = process.cwd()) {
    let current = resolve(start)
    for (;;) {
      for (const name of ['project.nqon', 'project.noqeri']) {
        const candidate = join(current, name)
        try { await stat(candidate); return candidate } catch {}
      }
      const parent = dirname(current)
      if (parent === current) throw new Error('project.nqon not found (legacy project.noqeri is also accepted)')
      current = parent
    }
  }

  function findBlock(text, name) {
    const match = new RegExp(`\\b${name}\\s*\\{`, 'm').exec(text)
    if (!match) return null
    let i = match.index + match[0].length, depth = 1, inString = false, escape = false
    for (; i < text.length; i++) {
      const c = text[i]
      if (inString) { if (escape) { escape = false; continue } if (c === '\\') { escape = true; continue } if (c === '"') inString = false; continue }
      if (c === '"') { inString = true; continue }
      if (c === '{') depth++
      if (c === '}' && --depth === 0) return { start: match.index, bodyStart: match.index + match[0].length, end: i }
    }
    throw new Error(`unterminated ${name} block`)
  }

  function dependencies(text) {
    const block = findBlock(text, 'dependencies')
    if (!block) return []
    const body = text.slice(block.bodyStart, block.end), result = []
    for (const match of body.matchAll(/^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:\s*"([^"]+)"\s*$/gm)) {
      const value = match[2], at = value.lastIndexOf('@')
      if (at > 0 && coordinatePattern.test(value.slice(0, at)) && versionPattern.test(value.slice(at + 1))) result.push({ alias: match[1], coordinate: value.slice(0, at), version: value.slice(at + 1) })
    }
    return result
  }

  function setDependency(text, coordinate, version) {
    const alias = packageAlias(coordinate)
    const line = '    ' + alias + ': "' + coordinate + '@' + version + '"'
    const block = findBlock(text, 'dependencies')
    if (!block) return text.replace(/\s*$/, '') + '\n\ndependencies {\n' + line + '\n}\n'
    const body = text.slice(block.bodyStart, block.end)
    const rx = new RegExp('^\\s*' + alias + '\\s*:\\s*"[^"]+"\\s*$', 'm')
    const next = rx.test(body) ? body.replace(rx, line) : body.replace(/\s*$/, '') + '\n' + line + '\n'
    return text.slice(0, block.bodyStart) + next + text.slice(block.end)
  }

  function nqonDependencies(text, source = 'project.nqon') {
    const manifest = parseNqon(text, { source })
    const block = manifest.packages ?? manifest.dependencies ?? Object.create(null)
    if (block === null || typeof block !== 'object' || Array.isArray(block)) throw new Error('project.nqon packages must be an object')
    const result = []
    for (const [alias, raw] of Object.entries(block)) {
      if (typeof raw !== 'string') throw new Error(`project.nqon package '${alias}' must be a package coordinate string`)
      const at = raw.lastIndexOf('@')
      const coordinate = at > 0 ? raw.slice(0, at) : raw
      const version = at > 0 ? raw.slice(at + 1) : ''
      if (!coordinatePattern.test(coordinate)) throw new Error(`invalid package coordinate for '${alias}': ${coordinate}`)
      if (version && !versionPattern.test(version)) throw new Error(`invalid package version for '${alias}': ${version}`)
      result.push({ alias, coordinate, version })
    }
    return result
  }

  function projectDependencies(text, projectPath) {
    return projectPath.endsWith('.nqon') ? nqonDependencies(text, projectPath) : dependencies(text)
  }

  function setProjectDependency(text, projectPath, coordinate, version) {
    if (!projectPath.endsWith('.nqon')) return setDependency(text, coordinate, version)
    const manifest = parseNqon(text, { source: projectPath })
    if (manifest.packages === undefined) manifest.packages = Object.create(null)
    if (manifest.packages === null || typeof manifest.packages !== 'object' || Array.isArray(manifest.packages)) throw new Error('project.nqon packages must be an object')
    manifest.packages[packageAlias(coordinate)] = `${coordinate}@${version}`
    return stringifyNqon(manifest)
  }

  async function writeLock(projectPath, items) {
    const sorted = [...items].sort((a, b) => compareText(a.coordinate, b.coordinate))
    let output = 'noqeri-lock 3\n'
    for (const item of sorted) output += `package ${item.alias} ${item.coordinate} ${item.version} ${item.integrity} ${item.entry}\n`
    await writeFile(join(dirname(projectPath), 'noqeri.lock'), output, { mode: 0o600 })
  }

  async function packageDependencies(pkg) {
    const manifest = await readFile(join(pkg.path, pkg.manifestName), 'utf8')
    return dependencies(manifest)
  }

  async function installGraph(rootDeps) {
    const resolved = new Map(), visiting = new Set()
    async function visit(dep, depth) {
      if (depth > 32) throw new Error(`dependency depth limit exceeded at ${dep.coordinate}`)
      const prior = resolved.get(dep.coordinate)
      if (prior) {
        if (prior.version !== dep.version) throw new Error(`dependency version conflict for ${dep.coordinate}: ${prior.version} vs ${dep.version}`)
        return
      }
      const key = `${dep.coordinate}@${dep.version}`
      if (visiting.has(key)) throw new Error(`dependency cycle detected at ${key}`)
      visiting.add(key)
      const pkg = await fetchPackage(dep.coordinate, dep.version)
      const item = { alias: dep.alias || packageAlias(dep.coordinate), coordinate: dep.coordinate, version: pkg.version, ...pkg }
      resolved.set(dep.coordinate, item)
      for (const child of await packageDependencies(pkg)) await visit(child, depth + 1)
      visiting.delete(key)
      console.log(`installed ${dep.coordinate}@${pkg.version}`)
    }
    for (const dep of rootDeps) await visit(dep, 0)
    return [...resolved.values()]
  }

  async function installProject() {
    const projectPath = await findProject(), text = await readFile(projectPath, 'utf8'), deps = projectDependencies(text, projectPath)
    const installed = await installGraph(deps)
    await writeLock(projectPath, installed)
    return installed
  }

  async function addPackage(spec) {
    const { coordinate, version } = parseCoordinate(spec), pkg = await fetchPackage(coordinate, version)
    const projectPath = await findProject(), text = await readFile(projectPath, 'utf8')
    await writeFile(projectPath, setProjectDependency(text, projectPath, coordinate, pkg.version))
    await installProject()
    console.log(`added ${coordinate}@${pkg.version}`)
  }

  async function resolvePackageImports(file) {
    const projectPath = await findProject(dirname(resolve(file))), projectText = await readFile(projectPath, 'utf8'), deps = projectDependencies(projectText, projectPath)
    const source = await readFile(file, 'utf8')
    const imports = [...source.matchAll(/\bimport\s+package\s+"([A-Za-z0-9_-]+\/[A-Za-z0-9_-]+)"/g)].map(match => match[1])
    const resolved = []
    for (const coordinate of imports) {
      const dep = deps.find(item => item.coordinate === coordinate)
      if (!dep) throw new Error(`package import ${coordinate} is not declared in project.nqon; run noqeri add ${coordinate}`)
      const pkg = await fetchPackage(coordinate, dep.version)
      const entry = join(pkg.path, ...safeRelative(pkg.entry).split('/'))
      await stat(entry)
      resolved.push({ coordinate, version: dep.version, entry, integrity: pkg.integrity })
    }
    return resolved
  }

  return { addPackage, installProject, resolvePackageImports, fetchPackage, cacheRoot }
}
