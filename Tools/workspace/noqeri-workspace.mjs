#!/usr/bin/env node
import { existsSync } from 'node:fs'
import { readFile } from 'node:fs/promises'
import { dirname, normalize, resolve, sep } from 'node:path'
import { spawnSync } from 'node:child_process'

function safeMember(root, value) {
  if (typeof value !== 'string' || !value || value.includes('\0')) throw new Error(`invalid workspace member: ${String(value)}`)
  const normalized = normalize(value)
  if (normalized === '..' || normalized.startsWith(`..${sep}`)) throw new Error(`workspace member escapes root: ${value}`)
  const full = resolve(root, normalized)
  const prefix = root.endsWith(sep) ? root : root + sep
  if (full !== root && !full.startsWith(prefix)) throw new Error(`workspace member escapes root: ${value}`)
  return full
}
export async function loadWorkspace(path = 'noqeri.workspace.json') {
  const full = resolve(path)
  const data = JSON.parse(await readFile(full, 'utf8'))
  if (data.schema !== 1 || !Array.isArray(data.members) || data.members.length === 0) throw new Error('workspace requires schema: 1 and a non-empty members array')
  const root = dirname(full)
  const members = data.members.map(value => safeMember(root, value))
  for (const member of members) if (!existsSync(resolve(member, 'project.noqeri'))) throw new Error(`workspace member has no project.noqeri: ${member}`)
  return { root, members }
}
async function main() {
  const args = process.argv.slice(2)
  const command = args[0]
  if (!command || command.startsWith('--')) throw new Error('usage: node Tools/workspace/noqeri-workspace.mjs <check|test|audit|doctor> [--workspace=FILE] [--continue]')
  if (!['check','test','audit','doctor'].includes(command)) throw new Error(`unsupported workspace command: ${command}`)
  const workspacePath = args.find(x => x.startsWith('--workspace='))?.slice(12) || 'noqeri.workspace.json'
  const keepGoing = args.includes('--continue')
  const binary = process.env.NOQERI_NATIVE || (process.platform === 'win32' ? resolve('build', 'noqeri.exe') : resolve('build', 'noqeri'))
  if (!existsSync(binary)) throw new Error(`Noqeri binary not found: ${binary}`)
  const workspace = await loadWorkspace(workspacePath)
  const results = []
  for (const member of workspace.members) {
    const commandArgs = command === 'check' ? ['check', 'project.noqeri'] : [command, '.']
    const result = spawnSync(binary, commandArgs, { cwd: member, stdio: 'inherit', env: process.env })
    const status = result.error ? 2 : (result.status ?? 2)
    results.push({ member, status })
    if (status !== 0 && !keepGoing) break
  }
  console.log(JSON.stringify({ command, results }, null, 2))
  process.exit(results.some(x => x.status !== 0) ? 1 : 0)
}
main().catch(error => { console.error(`noqeri workspace: ${error.message}`); process.exit(2) })
