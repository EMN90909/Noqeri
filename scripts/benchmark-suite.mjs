#!/usr/bin/env node
import { readFileSync, writeFileSync, mkdirSync, existsSync, statSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { performance } from 'node:perf_hooks'
import { resolve, dirname } from 'node:path'
import os from 'node:os'

function arg(name, fallback = '') {
  const prefix = `--${name}=`
  const value = process.argv.slice(2).find(x => x.startsWith(prefix))
  return value ? value.slice(prefix.length) : fallback
}
function median(values) {
  if (!values.length) return null
  const sorted = [...values].sort((a, b) => a - b)
  const middle = Math.floor(sorted.length / 2)
  return sorted.length % 2 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2
}
function percentile(values, p) {
  if (!values.length) return null
  const sorted = [...values].sort((a, b) => a - b)
  const index = Math.min(sorted.length - 1, Math.max(0, Math.ceil(p * sorted.length) - 1))
  return sorted[index]
}
function run(command, cwd, timeout) {
  const started = performance.now()
  const child = spawnSync(command[0], command.slice(1), {
    cwd,
    encoding: 'utf8',
    timeout,
    maxBuffer: 16 * 1024 * 1024,
    env: { ...process.env, NOQERI_BENCHMARK: '1' }
  })
  const elapsed = performance.now() - started
  return {
    elapsed_ms: elapsed,
    status: child.status,
    signal: child.signal,
    error: child.error?.message || '',
    stdout: (child.stdout || '').trim().slice(0, 4000),
    stderr: (child.stderr || '').trim().slice(0, 4000)
  }
}
function toolVersion(command, cwd) {
  if (!command?.length) return null
  const attempts = [[command[0], '--engine'], [command[0], '--version'], [command[0], 'version']]
  for (const candidate of attempts) {
    const result = spawnSync(candidate[0], candidate.slice(1), { cwd, encoding: 'utf8', timeout: 3000 })
    if (result.status === 0) return `${result.stdout || result.stderr || ''}`.trim().split('\n')[0].slice(0, 300)
  }
  return null
}
function readJson(path) { return JSON.parse(readFileSync(path, 'utf8')) }

const root = resolve(arg('root', process.cwd()))
const suitePath = resolve(root, arg('suite', 'Benchmarks/suite.json'))
const commandsPath = resolve(root, arg('commands', 'Benchmarks/commands.json'))
const outputPath = resolve(root, arg('output', 'build/benchmarks/latest.json'))
const timeout = Math.max(100, Number(arg('timeout', '120000')))
const only = new Set(arg('only', '').split(',').filter(Boolean))
const allowNonNativeNoqeri = arg('allow-non-native-noqeri', '0') === '1'
const nativeProofPath = resolve(root, arg('native-proof', 'build-native-compiler/native-compiler-proof.txt'))
let nativeQualification = {
  proof: nativeProofPath,
  present: false,
  qualified: false,
  fixed_point: false,
  steady_state_engine: null,
  vm_required_for_steady_state: null
}
if (existsSync(nativeProofPath)) {
  const proof = Object.fromEntries(
    readFileSync(nativeProofPath, 'utf8').split(/\r?\n/).filter(Boolean).map(line => {
      const index = line.indexOf('=')
      return index < 0 ? [line, ''] : [line.slice(0, index), line.slice(index + 1)]
    })
  )
  nativeQualification = {
    proof: nativeProofPath,
    present: true,
    qualified: proof.qualified === 'true',
    fixed_point: proof.fixed_point === 'true',
    steady_state_engine: proof.steady_state_engine || null,
    vm_required_for_steady_state: proof.vm_required_for_steady_state === 'true'
  }
}
const suite = readJson(suitePath)
if (!existsSync(commandsPath)) {
  console.error(`benchmark-suite: ${commandsPath} is missing. Copy Benchmarks/commands.example.json and point commands only at equivalent workload implementations.`)
  process.exit(2)
}
const commands = readJson(commandsPath)
const warmups = Math.max(0, Number(suite.policy?.warmups ?? 3))
const samples = Math.max(1, Number(suite.policy?.samples ?? 15))
const results = []
const failures = []
const versionCache = new Map()

for (const workload of suite.workloads || []) {
  if (only.size && !only.has(workload.id)) continue
  for (const language of suite.languages || []) {
    const entry = commands?.implementations?.[language]?.[workload.id]
    if (!entry || !Array.isArray(entry.command) || !entry.command.length) {
      results.push({ id: `${language}:${workload.id}`, language, workload: workload.id, evidence: 'unmeasured', reason: 'no-command', threshold_percent: workload.threshold_percent })
      continue
    }
    const cwd = resolve(root, entry.cwd || '.')
    const command = entry.command.map(x => `${x}`)
    const executable = entry.binary ? resolve(root, entry.binary) : null
    const engine = entry.engine || null
    const artifactKind = entry.artifact_kind || null
    const expectedArtifactKind = workload.id === 'compilation' ? 'native-compiler' : 'native-executable'
    const claimEligible = language !== 'noqeri' || (
      nativeQualification.present &&
      nativeQualification.qualified &&
      nativeQualification.fixed_point &&
      nativeQualification.steady_state_engine === 'selfhost-native-linux-x86_64' &&
      nativeQualification.vm_required_for_steady_state === false &&
      engine === 'selfhost-native-linux-x86_64' &&
      artifactKind === expectedArtifactKind
    )
    if (language === 'noqeri' && !claimEligible && !allowNonNativeNoqeri) {
      const reason = 'Noqeri comparative benchmarks require the fixed-point native qualification proof plus engine=selfhost-native-linux-x86_64 and the correct native artifact kind. Use --allow-non-native-noqeri=1 only for non-comparative diagnostics.'
      failures.push({ language, workload: workload.id, phase: 'configuration', reason })
      results.push({
        id: `${language}:${workload.id}`,
        language,
        workload: workload.id,
        evidence: 'failed',
        reason,
        engine,
        artifact_kind: artifactKind,
        claim_eligible: false,
        threshold_percent: workload.threshold_percent
      })
      continue
    }
    const versionKey = command[0]
    if (!versionCache.has(versionKey)) versionCache.set(versionKey, toolVersion(command, cwd))

    let failed = null
    for (let i = 0; i < warmups; i++) {
      const warm = run(command, cwd, timeout)
      if (warm.status !== 0) { failed = { phase: 'warmup', sample: i, ...warm }; break }
    }
    const raw = []
    if (!failed) {
      for (let i = 0; i < samples; i++) {
        const measured = run(command, cwd, timeout)
        if (measured.status !== 0) { failed = { phase: 'sample', sample: i, ...measured }; break }
        raw.push(measured.elapsed_ms)
      }
    }
    if (failed) {
      failures.push({ language, workload: workload.id, ...failed })
      results.push({ id: `${language}:${workload.id}`, language, workload: workload.id, evidence: 'failed', reason: failed.error || failed.stderr || `exit ${failed.status}`, threshold_percent: workload.threshold_percent })
      continue
    }
    let binaryBytes = null
    if (executable && existsSync(executable)) binaryBytes = statSync(executable).size
    results.push({
      id: `${language}:${workload.id}`,
      language,
      workload: workload.id,
      evidence: 'measured-local',
      threshold_percent: workload.threshold_percent,
      median_ms: median(raw),
      p95_ms: percentile(raw, 0.95),
      min_ms: Math.min(...raw),
      max_ms: Math.max(...raw),
      binary_bytes: binaryBytes,
      raw_samples_ms: raw,
      command,
      cwd,
      tool_version: versionCache.get(versionKey),
      engine,
      artifact_kind: artifactKind,
      claim_eligible: claimEligible
    })
  }
}

const report = {
  schema: 3,
  generated_at: new Date().toISOString(),
  methodology: 'Benchmarks/METHODOLOGY.md',
  suite: 'Benchmarks/suite.json',
  source_revision: process.env.NOQERI_SOURCE_REVISION || null,
  native_qualification: nativeQualification,
  environment: {
    platform: process.platform,
    arch: process.arch,
    os_release: os.release(),
    cpus: os.cpus().map(cpu => cpu.model),
    logical_cpus: os.cpus().length,
    total_memory_bytes: os.totalmem(),
    node: process.version,
    safety_mode: process.env.NOQERI_SAFETY_MODE || 'default',
    checked_overflow: process.env.NOQERI_CHECKED_OVERFLOW === '1'
  },
  policy: { warmups, samples, timeout_ms: timeout },
  results,
  failures
}
mkdirSync(dirname(outputPath), { recursive: true })
writeFileSync(outputPath, JSON.stringify(report, null, 2) + '\n')
console.log(`benchmark-suite: wrote ${outputPath}`)
console.log(`benchmark-suite: ${results.filter(x => x.evidence === 'measured-local').length} measured, ${results.filter(x => x.evidence === 'unmeasured').length} unmeasured, ${failures.length} failed`)
process.exit(failures.length ? 1 : 0)
