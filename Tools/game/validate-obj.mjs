#!/usr/bin/env node
// Small, bounded Wavefront OBJ geometry reader for the game SDK toolchain.
// Deliberately supports only 'v' and 'f' (position-only polygons). Unsupported
// records fail explicitly rather than silently dropping asset data.
import { readFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'


function sub(a, b) { return [a[0] - b[0], a[1] - b[1], a[2] - b[2]] }
function cross(a, b) {
  return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
}
function dot(a, b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] }

// Fan triangulation is only correct for planar convex faces. Reject other
// polygons rather than silently generate distorted/crossing triangles.
function assertPlanarConvexFace(ids, vertices, lineNumber) {
  if (ids.length === 3) return
  const points = ids.map(index => vertices[index])
  const p0 = points[0]
  const normal = cross(sub(points[1], p0), sub(points[2], p0))
  const normalLength = Math.sqrt(dot(normal, normal))
  if (!(normalLength > 0) || !Number.isFinite(normalLength)) {
    throw new Error(`OBJ line ${lineNumber}: degenerate polygon normal`)
  }
  const extent = Math.max(1, ...points.flatMap(p => p.map((v, i) => Math.abs(v - p0[i]))))
  for (const point of points) {
    if (Math.abs(dot(normal, sub(point, p0))) > 1e-9 * normalLength * extent) {
      throw new Error(`OBJ line ${lineNumber}: non-planar face is unsupported`)
    }
  }
  // Every consecutive turn must have the same winding as the first triangle.
  for (let i = 0; i < points.length; i++) {
    const a = points[i]
    const b = points[(i + 1) % points.length]
    const c = points[(i + 2) % points.length]
    const turn = dot(cross(sub(b, a), sub(c, b)), normal)
    if (!(turn > 0)) {
      throw new Error(`OBJ line ${lineNumber}: concave, degenerate or self-intersecting face`)
    }
  }
}

export function parseObj(source, { maxBytes = 4 * 1024 * 1024, maxVertices = 100000, maxTriangles = 200000 } = {}) {
  if (typeof source !== 'string') throw new TypeError('OBJ source must be UTF-8 text')
  for (const [name, value] of Object.entries({ maxBytes, maxVertices, maxTriangles })) {
    if (!Number.isSafeInteger(value) || value < 0) throw new RangeError(`${name} must be a nonnegative safe integer`)
  }
  if (Buffer.byteLength(source, 'utf8') > maxBytes) throw new Error('OBJ exceeds input byte limit')
  const vertices = []
  const triangles = []
  const boundsMin = [Infinity, Infinity, Infinity]
  const boundsMax = [-Infinity, -Infinity, -Infinity]
  const lines = source.split(/\r?\n/)
  for (let lineIndex = 0; lineIndex < lines.length; lineIndex++) {
    const raw = lines[lineIndex]
    if (raw.length > 4096) throw new Error(`OBJ line ${lineIndex + 1}: line is too long`)
    const line = raw.split('#', 1)[0].trim()
    if (!line) continue
    const tokens = line.split(/\s+/)
    if (tokens[0] === 'v') {
      if (tokens.length !== 4) throw new Error(`OBJ line ${lineIndex + 1}: vertex must have exactly 3 coordinates`)
      const point = tokens.slice(1).map(Number)
      if (point.some((v, i) => !Number.isFinite(v) || !/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/.test(tokens[i + 1]))) {
        throw new Error(`OBJ line ${lineIndex + 1}: invalid vertex coordinate`)
      }
      if (vertices.length >= maxVertices) throw new Error('OBJ vertex limit exceeded')
      vertices.push(point)
      for (let i = 0; i < 3; i++) {
        boundsMin[i] = Math.min(boundsMin[i], point[i])
        boundsMax[i] = Math.max(boundsMax[i], point[i])
      }
    } else if (tokens[0] === 'f') {
      if (tokens.length < 4 || tokens.length > 65) throw new Error(`OBJ line ${lineIndex + 1}: face must contain 3 to 64 vertices`)
      const ids = tokens.slice(1).map(token => {
        if (!/^[1-9]\d*$/.test(token)) throw new Error(`OBJ line ${lineIndex + 1}: only positive position indices are supported`)
        const id = Number(token) - 1
        if (!Number.isSafeInteger(id) || id >= vertices.length) throw new Error(`OBJ line ${lineIndex + 1}: vertex index out of range`)
        return id
      })
      assertPlanarConvexFace(ids, vertices, lineIndex + 1)
      for (let i = 1; i < ids.length - 1; i++) {
        const tri = [ids[0], ids[i], ids[i + 1]]
        if (new Set(tri).size !== 3) throw new Error(`OBJ line ${lineIndex + 1}: degenerate face`)
        const [a, b, c] = tri.map(index => vertices[index])
        const ab = b.map((value, axis) => value - a[axis])
        const ac = c.map((value, axis) => value - a[axis])
        const cross = [ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0]]
        const areaSquared = cross.reduce((sum, value) => sum + value * value, 0)
        if (!(areaSquared > 0) || !Number.isFinite(areaSquared)) throw new Error(`OBJ line ${lineIndex + 1}: zero-area or non-finite triangle`)
        if (triangles.length >= maxTriangles) throw new Error('OBJ triangle limit exceeded')
        triangles.push(tri)
      }
    } else {
      throw new Error(`OBJ line ${lineIndex + 1}: unsupported record '${tokens[0]}'`)
    }
  }
  if (vertices.length === 0 || triangles.length === 0) throw new Error('OBJ must contain vertices and faces')
  return { vertices, triangles, vertexCount: vertices.length, triangleCount: triangles.length, bounds: { min: boundsMin, max: boundsMax } }
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  const file = process.argv[2]
  if (!file || process.argv.length > 4 || (process.argv[3] && process.argv[3] !== '--json')) {
    console.error('Usage: node Tools/game/validate-obj.mjs <mesh.obj> [--json]')
    process.exitCode = 2
  } else {
    try {
      const bytes = await readFile(file)
      if (bytes.length > 4 * 1024 * 1024) throw new Error('OBJ exceeds input byte limit')
      const geometry = parseObj(new TextDecoder('utf-8', { fatal: true }).decode(bytes))
      const summary = { file, vertexCount: geometry.vertexCount, triangleCount: geometry.triangleCount, bounds: geometry.bounds }
      console.log(process.argv[3] === '--json' ? JSON.stringify(summary, null, 2) : `Validated ${file}: ${summary.vertexCount} vertices, ${summary.triangleCount} triangles`)
    } catch (error) {
      console.error(`Invalid OBJ: ${error.message}`)
      process.exitCode = 1
    }
  }
}
