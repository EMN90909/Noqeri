#!/usr/bin/env node
import { createHash } from 'node:crypto'
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
  const attempts = [[command[0], '--version'], [command[0], 'version']]
  for (const candidate of attempts) {
    const result = spawnSync(candidate[0], candidate.slice(1), { cwd, encoding: 'utf8', timeout: 3000 })
    if (result.status === 0) return `${result.stdout || result.stderr || ''}`.trim().split('\n')[0].slice(0, 300)
  }
  return null
}
function readJson(path) { return JSON.parse(readFileSync(path, 'utf8')) }
function sha256File(path) { return createHash('sha256').update(readFileSync(path)).digest('hex') }
function commandPath(cwd, command) {
  if (!command) return ''
  return command.includes('/') || command.includes('\\') ? resolve(cwd, command) : command
}
function failResult(language, workload, threshold, reason, phase = 'policy') {
  return {
    id: `${language}:${workload}`,
    language,
    workload,
    evidence: 'failed',
    reason,
    phase,
    threshold_percent: threshold
  }
}

const root = resolve(arg('root', process.cwd()))
const suitePath = resolve(root, arg('suite', 'Benchmarks/suite.json'))
const commandsPath = resolve(root, arg('commands', 'Benchmarks/commands.json'))
const outputPath = resolve(root, arg('output', 'build/benchmarks/latest.json'))
const timeout = Math.max(100, Number(arg('timeout', '120000')))
const only = new Set(arg('only', '').split(',').filter(Boolean))
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
    const isNoqeriRuntime = language === 'noqeri' && workload.id !== 'compilation'
    let provenance = null

    if (isNoqeriRuntime) {
      const violations = []
      if (entry.execution_model !== 'native-aot') violations.push('execution_model must be native-aot')
      if (!executable) violations.push('binary path is required')
      if (executable && commandPath(cwd, command[0]) !== executable) violations.push('timed command must directly execute the declared native binary')
      if (command.some(value => value.includes('.nqri') || value.includes('noqeri-nqri'))) violations.push('NQRI VM artifacts are not native AOT')
      if (command.slice(1).includes('run')) violations.push('noqeri run is the reference interpreter, not native AOT')
      if (!Array.isArray(entry.prepare) || !entry.prepare.length) violations.push('native AOT benchmark requires a prepare command')
      if (!entry.provenance) violations.push('native AOT benchmark requires provenance metadata')
      if (violations.length) {
        const failed = failResult(language, workload.id, workload.threshold_percent, violations.join('; '))
        failures.push(failed)
        results.push(failed)
        continue
      }
    }

    if (Array.isArray(entry.prepare) && entry.prepare.length) {
      const preparation = run(entry.prepare.map(x => `${x}`), cwd, timeout)
      if (preparation.status !== 0) {
        const reason = preparation.error || preparation.stderr || `prepare exited ${preparation.status}`
        const failed = failResult(language, workload.id, workload.threshold_percent, reason, 'prepare')
        failures.push({ ...failed, ...preparation })
        results.push(failed)
        continue
      }
    }

    if (isNoqeriRuntime) {
      const provenancePath = resolve(root, entry.provenance)
      if (!existsSync(provenancePath)) {
        const failed = failResult(language, workload.id, workload.threshold_percent, 'native AOT provenance file is missing')
        failures.push(failed)
        results.push(failed)
        continue
      }
      try {
        provenance = readJson(provenancePath)
      } catch (error) {
        const failed = failResult(language, workload.id, workload.threshold_percent, `invalid native AOT provenance: ${error.message}`)
        failures.push(failed)
        results.push(failed)
        continue
      }
      const violations = []
      if (provenance.execution_model !== 'native-aot') violations.push('provenance execution_model is not native-aot')
      if (typeof provenance.self_hosted_compiler !== 'boolean') violations.push('provenance must state self_hosted_compiler explicitly')
      if (!executable || !existsSync(executable)) violations.push('declared native executable is missing after prepare')
      if (executable && provenance.artifact && resolve(root, provenance.artifact) !== executable) violations.push('provenance artifact does not match timed binary')
      if (executable && existsSync(executable) && provenance.artifact_sha256 !== sha256File(executable)) violations.push('native executable SHA-256 does not match provenance')
      if (violations.length) {
        const failed = failResult(language, workload.id, workload.threshold_percent, violations.join('; '))
        failures.push(failed)
        results.push(failed)
        continue
      }
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
      execution_model: entry.execution_model || null,
      compiler_provenance: provenance?.compiler_provenance || null,
      self_hosted_compiler: provenance?.self_hosted_compiler ?? null,
      artifact_sha256: provenance?.artifact_sha256 || null,
      median_ms: median(raw),
      p95_ms: percentile(raw, 0.95),
      min_ms: Math.min(...raw),
      max_ms: Math.max(...raw),
      binary_bytes: binaryBytes,
      raw_samples_ms: raw,
      command,
      cwd,
      tool_version: versionCache.get(versionKey)
    })
  }
}

const report = {
  schema: 3,
  generated_at: new Date().toISOString(),
  methodology: 'Benchmarks/METHODOLOGY.md',
  suite: 'Benchmarks/suite.json',
  source_revision: process.env.NOQERI_SOURCE_REVISION || null,
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
  policy: { warmups, samples, timeout_ms: timeout, noqeri_runtime_requires: 'native-aot' },
  results,
  failures
}
mkdirSync(dirname(outputPath), { recursive: true })
writeFileSync(outputPath, JSON.stringify(report, null, 2) + '\n')
console.log(`benchmark-suite: wrote ${outputPath}`)
console.log(`benchmark-suite: ${results.filter(x => x.evidence === 'measured-local').length} measured, ${results.filter(x => x.evidence === 'unmeasured').length} unmeasured, ${failures.length} failed`)
process.exit(failures.length ? 1 : 0)
