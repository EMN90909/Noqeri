#!/usr/bin/env node
// Explicitly separates host-tool tests from Noqeri compiler/runtime proof.
import { spawnSync } from 'node:child_process'
import { existsSync, mkdirSync, writeFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const sourceOnly = process.argv.includes('--source-only')
const checks = []
function run(name, bin, args, expectedOutput = null) {
  const result = spawnSync(bin, args, { cwd: root, encoding: 'utf8', timeout: 120000, maxBuffer: 1024 * 1024 })
  const passed = !result.error && result.status === 0 && (expectedOutput === null || result.stdout.trim() === expectedOutput)
  checks.push({ name, passed, command: [bin, ...args], exitCode: result.status, error: result.error?.message || null, stdout: result.stdout?.slice(-6000) || '', stderr: result.stderr?.slice(-6000) || '' })
  return passed
}
run('OBJ loader unit and adversarial tests', process.execPath, ['--test', 'tests/game-obj.test.mjs'])
run('OBJ cube asset fixture', process.execPath, ['Tools/game/validate-obj.mjs', 'examples/game_sdk/assets/cube.obj'])
if (!sourceOnly) {
  const compiler = process.env.NOQERI_GAME_COMPILER || resolve(root, 'build/noqeri')
  if (existsSync(compiler)) run('Noqeri compiler checks game source', compiler, ['check', 'tests/game_foundations.noqeri'])
  else checks.push({ name: 'Noqeri compiler checks game source', passed: false, error: `compiler unavailable: ${compiler}` })
  const runner = process.env.NOQERI_GAME_RUNNER
  if (runner && existsSync(runner)) run('Noqeri runs gameplay fixture', runner, ['run', 'tests/game_foundations.noqeri'], '0')
  else checks.push({ name: 'Noqeri runs gameplay fixture', passed: false, error: 'set NOQERI_GAME_RUNNER to a verified runner supporting run' })
  checks.push({ name: 'Noqeri native 3D GPU frame', passed: false, error: 'native Noqeri SDL3/OpenGL renderer not implemented or verified' })
}
const report = { developmentVersion: '0.7.0', sourceOnly, qualifiedFor3dRelease: false, passed: checks.every(check => check.passed), checks }
mkdirSync(resolve(root, 'build'), { recursive: true })
writeFileSync(resolve(root, 'build/game-sdk-report.json'), JSON.stringify(report, null, 2) + '\n')
for (const check of checks) console.log(`${check.passed ? 'PASS' : 'BLOCKED'} ${check.name}${check.error ? ': ' + check.error : ''}`)
console.log('Report: build/game-sdk-report.json')
process.exitCode = report.passed ? 0 : 1
