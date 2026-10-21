import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = join(dirname(fileURLToPath(import.meta.url)), '..')
const pinPath = join(root, 'Compiler', 'selfhost', 'stage1-seed.sha256')
const provenancePath = join(root, 'Compiler', 'selfhost', 'stage1-seed.json')

function readPin() {
  const pin = readFileSync(pinPath, 'utf8').trim().split(/\s+/)[0].toLowerCase()
  assert.match(pin, /^[0-9a-f]{64}$/)
  return pin
}

test('Stage-1 seed has a canonical SHA-256 pin', () => {
  assert.equal(readPin().length, 64)
})

test('Stage-1 provenance matches the canonical pin and accepted bootstrap evidence', () => {
  const pin = readPin()
  const provenance = JSON.parse(readFileSync(provenancePath, 'utf8'))
  assert.equal(provenance.schema, 1)
  assert.equal(provenance.pin_algorithm, 'sha256')
  assert.equal(provenance.seed_sha256, pin)
  assert.ok(Number.isInteger(provenance.seed_bytes) && provenance.seed_bytes > 0)
  assert.match(provenance.source_commit, /^[0-9a-f]{40}$/)
  assert.ok(Number.isInteger(provenance.workflow_run_id) && provenance.workflow_run_id > 0)
  assert.equal(provenance.gcc_clang_stage1_byte_equal, true)
  assert.equal(provenance.stage2_stage3_byte_equal, true)
  assert.equal(provenance.stage2_sha256, provenance.stage3_sha256)
  assert.match(provenance.stage2_sha256, /^[0-9a-f]{64}$/)
  assert.equal(provenance.stage1_independently_trusted, false)
})
