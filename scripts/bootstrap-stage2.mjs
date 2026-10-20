#!/usr/bin/env node
import { createHash } from 'node:crypto'
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { spawnSync } from 'node:child_process'

function option(name, fallback = '') {
  const prefix = `--${name}=`
  const hit = process.argv.slice(2).find(x => x.startsWith(prefix))
  return hit ? hit.slice(prefix.length) : fallback
}
function sha256File(path) {
  const hash = createHash('sha256')
  hash.update(readFileSync(path))
  return hash.digest('hex')
}
function sha256CompilerSources(root, manifestPath) {
  const hash = createHash('sha256')
  const lines = readFileSync(manifestPath, 'utf8').split(/\r?\n/).map(x => x.trim()).filter(Boolean)
  for (const relative of lines) {
    const path = resolve(root, relative)
    if (!existsSync(path)) throw new Error(`self-host source is missing: ${relative}`)
    hash.update(relative)
    hash.update('\0')
    hash.update(readFileSync(path))
    hash.update('\0')
  }
  return { sha256: hash.digest('hex'), files: lines }
}
function runStep(root, name, command, timeout) {
  if (!Array.isArray(command) || command.length === 0) {
    return { name, passed: false, configured: false, status: null, signal: null, elapsed_ms: 0, stdout: '', stderr: 'step is not configured' }
  }
  const started = Date.now()
  const result = spawnSync(command[0], command.slice(1), {
    cwd: root,
    encoding: 'utf8',
    timeout,
    maxBuffer: 32 * 1024 * 1024,
    env: { ...process.env, NOQERI_STAGE2_VERIFY: '1', SOURCE_DATE_EPOCH: process.env.SOURCE_DATE_EPOCH || '0' }
  })
  return {
    name,
    command,
    configured: true,
    passed: result.status === 0 && !result.error && !result.signal,
    status: result.status,
    signal: result.signal,
    elapsed_ms: Date.now() - started,
    error: result.error?.message || '',
    stdout: (result.stdout || '').slice(-12000),
    stderr: (result.stderr || '').slice(-12000)
  }
}
function requireFile(root, value, label) {
  if (!value) return { path: null, exists: false, label }
  const path = resolve(root, value)
  return { path, exists: existsSync(path), label }
}

const root = resolve(option('root', process.cwd()))
const configPath = resolve(root, option('config', 'Compiler/selfhost/bootstrap-local.json'))
const outputPath = resolve(root, option('output', 'build/stage2-proof.json'))
const timeout = Math.max(1000, Number(option('timeout', '600000')))
const sourceManifest = resolve(root, 'Compiler/selfhost/compiler-sources.txt')

if (!existsSync(configPath)) {
  console.error(`bootstrap-stage2: ${configPath} is missing`)
  console.error('Copy Compiler/selfhost/bootstrap.example.json to bootstrap-local.json and configure actual Stage-1/Stage-2 compiler commands. The gate intentionally cannot pass without executable bootstrap evidence.')
  process.exit(2)
}

let config
try { config = JSON.parse(readFileSync(configPath, 'utf8')) }
catch (error) { console.error(`bootstrap-stage2: invalid config: ${error.message}`); process.exit(2) }

const sourceClosure = sha256CompilerSources(root, sourceManifest)
const stages = []
const stage0Configured = config.stage0_built_stage1 === true && Array.isArray(config.commands?.stage0_builds_stage1) && config.commands.stage0_builds_stage1.length > 0
const stage0 = runStep(root, 'stage0-builds-stage1', config.commands?.stage0_builds_stage1, timeout)
stages.push(stage0)
const stage1 = stage0Configured && stage0.passed
  ? runStep(root, 'stage1-builds-stage2', config.commands?.stage1_builds_stage2, timeout)
  : { name: 'stage1-builds-stage2', passed: false, skipped: true, stderr: 'stage0 provenance has not passed' }
stages.push(stage1)
const stage2 = stage1.passed ? runStep(root, 'stage2-rebuilds-compiler', config.commands?.stage2_rebuilds_compiler, timeout) : { name: 'stage2-rebuilds-compiler', passed: false, configured: Array.isArray(config.commands?.stage2_rebuilds_compiler), skipped: true, stderr: 'stage1 build failed' }
stages.push(stage2)

const stage2Artifact = requireFile(root, config.artifacts?.stage2, 'stage2')
const rebuiltArtifact = requireFile(root, config.artifacts?.rebuilt, 'rebuilt-stage2')
let outputEquivalent = false
let stage2Sha256 = null
let rebuiltSha256 = null
if (stage1.passed && stage2.passed && stage2Artifact.exists && rebuiltArtifact.exists) {
  stage2Sha256 = sha256File(stage2Artifact.path)
  rebuiltSha256 = sha256File(rebuiltArtifact.path)
  outputEquivalent = stage2Sha256 === rebuiltSha256
  if (!outputEquivalent && Array.isArray(config.commands?.semantic_equivalence) && config.commands.semantic_equivalence.length) {
    const equivalence = runStep(root, 'semantic-equivalence', config.commands.semantic_equivalence, timeout)
    stages.push(equivalence)
    outputEquivalent = equivalence.passed
  }
}

const requiredSuites = [
  ['frontend', config.suites?.frontend],
  ['nir', config.suites?.nir],
  ['optimizer', config.suites?.optimizer],
  ['object-writers', config.suites?.object_writers],
  ['formatter', config.suites?.formatter],
  ['lsp', config.suites?.lsp],
  ['package-manager', config.suites?.package_manager],
  ['fuzz', config.suites?.fuzz],
  ['race', config.suites?.race],
  ['memory', config.suites?.memory],
  ['overflow', config.suites?.overflow],
  ['reproducible-build', config.suites?.reproducible_build],
  ['practical-corpus', config.suites?.practical_corpus]
]
const suites = []
if (stage1.passed && stage2.passed && outputEquivalent) {
  for (const [name, command] of requiredSuites) suites.push(runStep(root, name, command, timeout))
} else {
  for (const [name, command] of requiredSuites) suites.push({ name, passed: false, configured: Array.isArray(command) && command.length > 0, skipped: true, stderr: 'bootstrap equivalence has not passed' })
}

const suiteMap = Object.fromEntries(suites.map(x => [x.name, x.passed]))
const releaseNeedsCpp = config.release_needs_cpp !== false
const allSuitesPassed = suites.every(x => x.passed)
const verified = stage0Configured && stage0.passed && stage1.passed && stage2.passed && outputEquivalent && allSuitesPassed && !releaseNeedsCpp

const proof = {
  schema: 1,
  verified,
  generated_at: new Date().toISOString(),
  source_manifest: 'Compiler/selfhost/compiler-sources.txt',
  source_closure_sha256: sourceClosure.sha256,
  source_files: sourceClosure.files,
  release_needs_cpp: releaseNeedsCpp,
  stage0_built_stage1: stage0Configured && stage0.passed,
  stage1_built_stage2: stage1.passed,
  stage2_rebuilt_compiler: stage2.passed,
  stage2_output_sha256: stage2Sha256,
  rebuilt_output_sha256: rebuiltSha256,
  outputs_equivalent: outputEquivalent,
  frontend_passed: Boolean(suiteMap.frontend),
  nir_passed: Boolean(suiteMap.nir),
  optimizer_passed: Boolean(suiteMap.optimizer),
  object_writers_passed: Boolean(suiteMap['object-writers']),
  formatter_passed: Boolean(suiteMap.formatter),
  lsp_passed: Boolean(suiteMap.lsp),
  package_manager_passed: Boolean(suiteMap['package-manager']),
  hardening: {
    fuzz_passed: Boolean(suiteMap.fuzz),
    race_passed: Boolean(suiteMap.race),
    memory_passed: Boolean(suiteMap.memory),
    overflow_passed: Boolean(suiteMap.overflow),
    reproducible_build_passed: Boolean(suiteMap['reproducible-build']),
    practical_corpus_passed: Boolean(suiteMap['practical-corpus'])
  },
  artifacts: {
    stage2: { configured: Boolean(config.artifacts?.stage2), exists: stage2Artifact.exists, path: config.artifacts?.stage2 || null },
    rebuilt: { configured: Boolean(config.artifacts?.rebuilt), exists: rebuiltArtifact.exists, path: config.artifacts?.rebuilt || null }
  },
  stages,
  suites
}
mkdirSync(dirname(outputPath), { recursive: true })
writeFileSync(outputPath, JSON.stringify(proof, null, 2) + '\n')
console.log(`bootstrap-stage2: wrote ${outputPath}`)
console.log(`bootstrap-stage2: verified=${verified} equivalent=${outputEquivalent} suites=${suites.filter(x => x.passed).length}/${suites.length} release_needs_cpp=${releaseNeedsCpp}`)
if (!verified) {
  console.error('bootstrap-stage2: Stage 2 is NOT verified. C++ bootstrap provenance must not be removed or described as unnecessary yet.')
}
process.exit(verified ? 0 : 1)
