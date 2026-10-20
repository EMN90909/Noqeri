#!/usr/bin/env node
import assert from 'node:assert/strict'
import { parseLock, versionAffected } from '../../Runtime/ecosystem-tools.mjs'

const hash = 'sha256:' + 'a'.repeat(64)
const v3 = parseLock(`noqeri-lock 3
package json noqeri/json 1.2.3 ${hash} src/json.noqeri
`)
assert.equal(v3.version, 3)
assert.deepEqual(v3.packages[0], { alias: 'json', coordinate: 'noqeri/json', version: '1.2.3', integrity: hash, entry: 'src/json.noqeri', lockVersion: 3 })

const v2 = parseLock(`noqeri-lock 2
project demo 0.1.0 edition 2026 target x86_64-unknown-none
dependency json noqeri/json 1.2.3 source registry checksum ${hash}
`)
assert.equal(v2.version, 2)
assert.equal(v2.packages[0].coordinate, 'noqeri/json')
assert.equal(v2.packages[0].integrity, hash)
assert.equal(versionAffected('1.2.3', '>=1.0.0 <2.0.0'), true)
assert.equal(versionAffected('2.0.0', '>=1.0.0 <2.0.0'), false)
assert.equal(versionAffected('2.1.0', '<1.5.0 || >=2.0.0 <2.2.0'), true)
assert.throws(() => parseLock('noqeri-lock 99\n'), /unsupported lock/)
assert.throws(() => parseLock(`noqeri-lock 3\npackage json noqeri/json 1.2.3 sha256:bad src/json.noqeri\n`), /invalid sha256/)
console.log('ecosystem tooling tests passed')
