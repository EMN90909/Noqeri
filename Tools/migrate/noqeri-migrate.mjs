#!/usr/bin/env node
import { readFile, readdir, stat, writeFile } from 'node:fs/promises'
import { join, resolve } from 'node:path'

const SKIP = new Set(['.git', 'build', '.noqeri', 'vendor', 'node_modules'])
async function sources(root) {
  const out = []
  async function walk(dir) {
    for (const name of await readdir(dir)) {
      if (SKIP.has(name)) continue
      const path = join(dir, name)
      const info = await stat(path)
      if (info.isDirectory()) await walk(path)
      else if (info.isFile() && path.endsWith('.noqeri')) out.push(path)
    }
  }
  await walk(root)
  return out.sort()
}
function applyRules(text, rules) {
  let current = text
  const applied = []
  for (const rule of rules) {
    if (typeof rule.from !== 'string' || !rule.from) throw new Error(`migration ${rule.id} has an empty 'from' token`)
    const count = current.split(rule.from).length - 1
    if (!count) continue
    current = current.split(rule.from).join(String(rule.to ?? ''))
    applied.push({ id: rule.id, replacements: count })
  }
  return { text: current, applied }
}
async function main() {
  const args = process.argv.slice(2)
  const root = resolve(args.find(x => !x.startsWith('--')) || '.')
  const rulesPath = resolve(args.find(x => x.startsWith('--rules='))?.slice(8) || 'migrations/index.json')
  const write = args.includes('--write')
  const requested = new Set(args.filter(x => x.startsWith('--id=')).map(x => x.slice(5)))
  const index = JSON.parse(await readFile(rulesPath, 'utf8'))
  if (index.schema !== 1 || !Array.isArray(index.migrations)) throw new Error('migration index requires schema: 1 and migrations[]')
  let rules = index.migrations
  if (requested.size) rules = rules.filter(x => requested.has(x.id))
  const results = []
  for (const file of await sources(root)) {
    const before = await readFile(file, 'utf8')
    const changed = applyRules(before, rules)
    if (!changed.applied.length) continue
    if (write) await writeFile(file, changed.text)
    results.push({ file, applied: changed.applied })
  }
  console.log(JSON.stringify({ mode: write ? 'write' : 'dry-run', files_changed: results.length, results }, null, 2))
}
main().catch(error => { console.error(`noqeri migrate: ${error.message}`); process.exit(2) })
