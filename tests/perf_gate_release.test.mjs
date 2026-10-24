import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import { spawnSync } from 'node:child_process'

const gate = resolve('scripts/perf-gate.mjs')
const environment = {
  platform: 'linux', arch: 'x64', os_release: 'test-kernel',
  cpus: ['same-cpu'], logical_cpus: 1, total_memory_bytes: 1024,
  node: 'v22', safety_mode: 'default', checked_overflow: false
}
function report(median = 10, p95 = 10) {
  return {
    schema: 3, source_revision: 'test-revision', environment, failures: [],
    results: [{
      id: 'noqeri:sorting', evidence: 'measured-local',
      execution_model: 'native-aot', self_hosted_compiler: false,
      validated_stdout: 'correct', artifact_sha256: 'abc',
      threshold_percent: 8, median_ms: median, p95_ms: p95,
      raw_samples_ms: [...Array(14).fill(median), p95]
    }]
  }
}
function run(before, after, extras = []) {
  const dir = mkdtempSync(join(tmpdir(), 'noqeri-release-gate-'))
  try {
    const a = join(dir, 'baseline.json'), b = join(dir, 'candidate.json')
    writeFileSync(a, JSON.stringify(before))
    writeFileSync(b, JSON.stringify(after))
    const required = extras.find(x => x.startsWith('--require=')) ||
      '--require=noqeri:sorting'
    return spawnSync(process.execPath,
      [gate, a, b, '--release', required,
       ...extras.filter(x => !x.startsWith('--require='))],
      { encoding: 'utf8' })
  } finally { rmSync(dir, { recursive: true, force: true }) }
}
test('release gate accepts matching measured same-host evidence', () => {
  const result = run(report(), report(9.8, 10))
  assert.equal(result.status, 0, result.stdout + result.stderr)
})
test('release gate refuses machine mismatch and absent raw measurements', () => {
  const candidate = report()
  candidate.environment = { ...environment, arch: 'arm64' }
  candidate.results[0].raw_samples_ms = [10]
  const result = run(report(), candidate)
  assert.equal(result.status, 2)
  assert.match(result.stdout, /mismatched: arch/)
  assert.match(result.stdout, /at least 10 valid raw samples/)
})
test('release gate refuses missing required benchmark evidence', () => {
  const after = report()
  after.results = []
  assert.equal(run(report(), after).status, 2)
})
test('release gate detects p95 regression even when median is unchanged', () => {
  const result = run(report(), report(10, 13))
  assert.equal(result.status, 1)
  assert.match(result.stdout, /p95_change_percent/)
})
test('release gate refuses synthetic VM data under a native label', () => {
  const after = report()
  after.results[0].execution_model = 'nqri-vm'
  assert.equal(run(report(), after).status, 2)
})
test('release mode requires explicit workload coverage', () => {
  const result = run(report(), report(), ['--require='])
  assert.equal(result.status, 2)
})
