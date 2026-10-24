import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { dirname, join } from 'node:path'
import { tmpdir } from 'node:os'
import { fileURLToPath } from 'node:url'

const root = join(dirname(fileURLToPath(import.meta.url)), '..')
const config = JSON.parse(readFileSync(join(root, 'Benchmarks', 'commands.example.json'), 'utf8'))
const noqeri = config.implementations?.noqeri || {}

test('Noqeri runtime benchmark examples use standalone native AOT executables', () => {
  for (const [workload, entry] of Object.entries(noqeri)) {
    if (workload === 'compilation') continue
    assert.equal(entry.execution_model, 'native-aot', `${workload}: execution_model`)
    assert.ok(Array.isArray(entry.prepare) && entry.prepare.includes('scripts/native-build.mjs'), `${workload}: native prepare`)
    assert.equal(entry.command?.[0], entry.binary, `${workload}: timed command must be the native binary`)
    assert.equal(typeof entry.provenance, 'string', `${workload}: provenance path`)
    assert.equal(typeof entry.expected_stdout, 'string', `${workload}: deterministic validation output`)
    assert.ok(!entry.command.some(arg => String(arg).includes('.nqri')), `${workload}: NQRI is not native AOT`)
    assert.ok(!entry.command.some(arg => String(arg).includes('noqeri-nqri')), `${workload}: VM must not be timed`)
    assert.ok(!entry.command.slice(1).includes('run'), `${workload}: interpreter must not be timed`)
  }
})

test('benchmark harness fails closed on the old interpreter command', () => {
  const temp = mkdtempSync(join(tmpdir(), 'noqeri-native-policy-'))
  try {
    const commandsPath = join(temp, 'commands.json')
    const reportPath = join(temp, 'report.json')
    writeFileSync(commandsPath, JSON.stringify({
      schema: 2,
      implementations: {
        noqeri: {
          startup: {
            command: ['build/noqeri', 'run', 'Benchmarks/workloads/startup.noqeri'],
            binary: 'build/noqeri',
            expected_stdout: '0'
          }
        },
        c: {}, cpp: {}, rust: {}, zig: {}, go: {}
      }
    }))
    const result = spawnSync(process.execPath, [
      'scripts/benchmark-suite.mjs',
      `--root=${root}`,
      `--commands=${commandsPath}`,
      `--output=${reportPath}`,
      '--only=startup'
    ], { cwd: root, encoding: 'utf8' })
    assert.equal(result.status, 1)
    const report = JSON.parse(readFileSync(reportPath, 'utf8'))
    const rejected = report.results.find(row => row.language === 'noqeri' && row.workload === 'startup')
    assert.equal(rejected?.evidence, 'failed')
    assert.match(rejected?.reason || '', /native-aot|reference interpreter/)
  } finally {
    rmSync(temp, { recursive: true, force: true })
  }
})

test('NQRI bootstrap cannot masquerade as native self-hosting', () => {
  const nativeSh = readFileSync(join(root, 'scripts', 'bootstrap-native.sh'), 'utf8')
  const nqriSh = readFileSync(join(root, 'scripts', 'bootstrap-nqri.sh'), 'utf8')
  const buildSh = readFileSync(join(root, 'scripts', 'build.sh'), 'utf8')
  assert.match(nativeSh, /intentionally disabled/)
  assert.match(nativeSh, /exit 2/)
  assert.match(buildSh, /bootstrap-nqri\.sh/)
  assert.match(nqriSh, /execution_model=nqri-vm/)
  assert.match(nqriSh, /native_codegen=false/)
  assert.match(nqriSh, /native_selfhost=false/)
})

test('compiler timing is explicitly separated from runtime timing', () => {
  assert.equal(noqeri.compilation?.execution_model, 'compiler-stage0')
  assert.ok(noqeri.compilation?.command?.includes('check'))
})

test('strict benchmark coverage rejects an unmeasured required workload', () => {
  const temp = mkdtempSync(join(tmpdir(), 'noqeri-required-workload-'))
  try {
    const commandsPath = join(temp, 'commands.json')
    const reportPath = join(temp, 'report.json')
    writeFileSync(commandsPath, JSON.stringify({ schema: 2, implementations: {} }))
    const result = spawnSync(process.execPath, [
      'scripts/benchmark-suite.mjs', `--root=${root}`,
      `--commands=${commandsPath}`, `--output=${reportPath}`,
      '--only=startup', '--require-workloads=noqeri:startup'
    ], { cwd: root, encoding: 'utf8' })
    assert.equal(result.status, 1)
    const report = JSON.parse(readFileSync(reportPath, 'utf8'))
    assert.ok(report.failures.some(row => row.phase === 'coverage' &&
      row.reason.includes('noqeri:startup')))
  } finally {
    rmSync(temp, { recursive: true, force: true })
  }
})
