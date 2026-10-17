import test from 'node:test'
import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import { parseObj } from '../Tools/game/validate-obj.mjs'

const sample = fileURLToPath(new URL('../examples/game_sdk/assets/cube.obj', import.meta.url))
test('triangulates shipped cube and computes bounds', async () => {
  const mesh = parseObj(await readFile(sample, 'utf8'))
  assert.equal(mesh.vertexCount, 8)
  assert.equal(mesh.triangleCount, 12)
  assert.deepEqual(mesh.bounds, { min: [-1, -1, -1], max: [1, 1, 1] })
  assert.equal(mesh.triangles.length, 12)
})
test('rejects malformed faces, unsafe indices and unsupported records', () => {
  for (const source of ['v 0 0 0\nf 1 2 3', 'v 0 0 0\nv 1 0 0\nv 2 0 0\nf 1 2 3', 'v 0 0 0\nvt 0 0', 'v NaN 0 0\nf 1 1 1', 'v 0 0 0\nv 0 1 0\nv 1 0 0\nf -1 2 3']) {
    assert.throws(() => parseObj(source))
  }
})
test('applies vertex, triangle and byte limits', () => {
  assert.throws(() => parseObj('v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3', { maxVertices: 2 }))
  assert.throws(() => parseObj('v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3', { maxTriangles: 0 }))
  assert.throws(() => parseObj('x'.repeat(20), { maxBytes: 10 }))
})
test('triangulates convex quads and rejects zero direction', () => {
  const mesh = parseObj('v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3 4')
  assert.deepEqual(mesh.triangles, [[0, 1, 2], [0, 2, 3]])
})
