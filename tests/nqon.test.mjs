import test from 'node:test'
import assert from 'node:assert/strict'
import { parseNqon, stringifyNqon } from '../Runtime/nqon.mjs'

test('NQON keeps the common path small', () => {
  const data = parseNqon(`
name = Aroeno
version = "1.0"
private = true

packages {
    forge = "1.2"
    postgres = "2.0"
}

platforms = [linux windows "mac os"]
`)
  assert.equal(data.name, 'Aroeno')
  assert.equal(data.version, '1.0')
  assert.equal(data.private, true)
  assert.deepEqual({ ...data.packages }, { forge: '1.2', postgres: '2.0' })
  assert.deepEqual(data.platforms, ['linux', 'windows', 'mac os'])
})

test('NQON supports comments, explicit objects and optional commas', () => {
  const data = parseNqon(`
// line comments use the same spelling as Noqeri
server = {
  host = localhost,
  port = 8080
  enabled = true
}
/* block comments are allowed too */
`)
  assert.equal(data.server.host, 'localhost')
  assert.equal(data.server.port, 8080)
  assert.equal(data.server.enabled, true)
})

test('NQON serialization round-trips without changing value types', () => {
  const source = Object.assign(Object.create(null), {
    name: 'Noqeri',
    version: '1.0',
    count: 7,
    enabled: true,
    nothing: null,
    nested: Object.assign(Object.create(null), { path: 'src/main.noqeri' }),
    values: ['linux', 'windows', 'mac os']
  })
  const text = stringifyNqon(source)
  const parsed = parseNqon(text)
  assert.deepEqual(JSON.parse(JSON.stringify(parsed)), JSON.parse(JSON.stringify(source)))
})

test('NQON quotes comment-like URL strings during serialization', () => {
  const source = Object.assign(Object.create(null), {
    registry: 'https://github.com/EMN90909/noqeri-registry',
    marker: 'left/*right'
  })
  const text = stringifyNqon(source)
  assert.match(text, /registry = "https:\/\/github\.com\//)
  assert.match(text, /marker = "left\/\*right"/)
  const parsed = parseNqon(text)
  assert.equal(parsed.registry, source.registry)
  assert.equal(parsed.marker, source.marker)
})

test('NQON rejects duplicate keys and malformed input', () => {
  assert.throws(() => parseNqon('name = one\nname = two\n'), /duplicate key/)
  assert.throws(() => parseNqon('packages { forge = 1.0\n'), /expected '\}'/)
  assert.throws(() => parseNqon('name: nope\n'), /expected '=' or '\{'/)
})
