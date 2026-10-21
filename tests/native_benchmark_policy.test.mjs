import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
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
    assert.ok(!entry.command.some(arg => String(arg).includes('.nqri')), `${workload}: NQRI is not native AOT`)
    assert.ok(!entry.command.some(arg => String(arg).includes('noqeri-nqri')), `${workload}: VM must not be timed`)
    assert.ok(!entry.command.slice(1).includes('run'), `${workload}: interpreter must not be timed`)
  }
})

test('compiler timing is explicitly separated from runtime timing', () => {
  assert.equal(noqeri.compilation?.execution_model, 'compiler-stage0')
  assert.ok(noqeri.compilation?.command?.includes('check'))
})
