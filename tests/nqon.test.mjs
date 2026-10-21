import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import { spawnSync } from 'node:child_process'
import { parseNqon, stringifyNqon, NqonSyntaxError } from '../Runtime/nqon.mjs'

test('parses beginner-friendly project syntax', () => {
  const value = parseNqon(`
// Noqeri project
name = Aroeno
version = "1.0.0"
private = true

packages {
    forge = "noqeri/forge@1.2.0"
    postgres = "noqeri/postgres@2.0.0"
}
`)
  assert.equal(value.name, 'Aroeno')
  assert.equal(value.version, '1.0.0')
  assert.equal(value.private, true)
  assert.equal(value.packages.forge, 'noqeri/forge@1.2.0')
})

test('supports arrays, anonymous objects and optional commas', () => {
  const value = parseNqon(`platforms = [linux, windows macos]\npeople = [{ name = Ada active = true } { name = Linus active = false }]`)
  assert.deepEqual([...value.platforms], ['linux', 'windows', 'macos'])
  assert.equal(value.people[0].name, 'Ada')
  assert.equal(value.people[1].active, false)
})

test('stringify round-trips data', () => {
  const input = { name: 'Noqeri', version: '0.7.0', private: false, packages: { forge: 'noqeri/forge@1.2.0' }, targets: ['linux', 'windows'] }
  const output = stringifyNqon(input)
  const parsed = parseNqon(output)
  assert.deepEqual(JSON.parse(JSON.stringify(parsed)), input)
})

test('rejects duplicates and malformed structure', () => {
  assert.throws(() => parseNqon('name = A\nname = B\n'), NqonSyntaxError)
  assert.throws(() => parseNqon('packages { forge = true'), NqonSyntaxError)
  assert.throws(() => parseNqon('name: Aroeno'), NqonSyntaxError)
})

test('standalone CLI converts NQON without a Noqeri compiler', () => {
  const root = mkdtempSync(join(tmpdir(), 'nqon-'))
  try {
    const file = join(root, 'project.nqon')
    writeFileSync(file, 'name = Aroeno\nversion = "1.0.0"\n')
    const cli = resolve('Runtime/nqon-cli.mjs')
    const result = spawnSync(process.execPath, [cli, 'to-json', file], { encoding: 'utf8' })
    assert.equal(result.status, 0, result.stderr)
    assert.deepEqual(JSON.parse(result.stdout), { name: 'Aroeno', version: '1.0.0' })
  } finally {
    rmSync(root, { recursive: true, force: true })
  }
})
