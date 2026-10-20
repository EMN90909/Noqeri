#!/usr/bin/env node
import fs from 'node:fs'
import path from 'node:path'

const minimumBytes = 20 * 1024
const minimumExports = 20
const modules = ['core.noqeri', 'compare.noqeri', 'counter.noqeri', 'crypto.noqeri', 'adler.noqeri', 'bit.noqeri', 'checksum.noqeri', 'algorithm.noqeri']
let failed = false
for (const name of modules) {
  const file = path.join(process.cwd(), 'Lib', 'std', name)
  const source = fs.readFileSync(file, 'utf8')
  const bytes = Buffer.byteLength(source)
  const code = source.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '')
  const exports = [...code.matchAll(/^\s*export\s+function\s+[A-Za-z_][A-Za-z0-9_]*\s*(?:<[^\n{]*>)?\s*\(/gm)].length
  const records = [...code.matchAll(/^\s*record\s+[A-Za-z_][A-Za-z0-9_]*/gm)].length
  const loops = [...code.matchAll(/\bwhile\b/g)].length
  const branches = [...code.matchAll(/\bif\b/g)].length
  const ok = bytes >= minimumBytes && exports >= minimumExports && (records + loops + branches) >= 10
  console.log(`${name}: ${bytes} bytes, ${exports} exports, ${records} records, ${loops} loops, ${branches} branches -> ${ok ? 'OK' : 'FAIL'}`)
  if (!ok) failed = true
}
if (failed) process.exit(1)
console.log('deep foundations gate passed')
