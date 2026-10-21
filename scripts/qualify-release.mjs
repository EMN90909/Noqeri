#!/usr/bin/env node
// Noqeri general-purpose release qualification. Independent game-engine
// certification belongs to scripts/check-game-sdk.mjs; game SDK tests are
// still included here to catch regressions in optional packages.
import { spawnSync } from 'node:child_process'
import { existsSync, mkdirSync, readdirSync, writeFileSync } from 'node:fs'
import { resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('..', import.meta.url))
const reportDir = resolve(root, 'build/release-qualification')
mkdirSync(reportDir, { recursive: true })
const steps = []
function blocked(name, error) {
  steps.push({ name, passed: false, error })
  console.error(`BLOCKED ${name}: ${error}`)
}
function run(name, command, args, cwd = root, env = {}) {
  const start = Date.now()
  const result = spawnSync(command, args, {
    cwd, encoding: 'utf8', timeout: 900000, maxBuffer: 8 * 1024 * 1024,
    env: { ...process.env, ...env }
  })
  const entry = {
    name, command: [command, ...args], status: result.status, signal: result.signal,
    elapsed_ms: Date.now() - start,
    passed: result.status === 0 && !result.error && !result.signal,
    error: result.error?.message || null,
    stdout: (result.stdout || '').slice(-30000),
    stderr: (result.stderr || '').slice(-30000)
  }
  steps.push(entry)
  console.log(`${entry.passed ? 'PASS' : 'FAIL'} ${name}${entry.error ? ': ' + entry.error : ''}`)
  return entry.passed
}

// Standalone JavaScript behavior must pass regardless of compiler availability.
run('web-component-regressions', process.execPath, ['--test', 'tests/web-compiler.test.mjs', 'tests/web-project.test.mjs'])
run('nqon-format-regressions', process.execPath, ['--test', 'tests/nqon.test.mjs'])
run('optional-game-asset-tests', process.execPath, ['--test', 'tests/game-obj.test.mjs'])
const testsDir = resolve(root, 'tests')
const noesqlTests = existsSync(testsDir)
  ? readdirSync(testsDir).filter(file => /^noesql(?:-.*)?\.test\.mjs$/.test(file)).sort()
  : []
if (noesqlTests.length) run('noesql-reference-regressions', process.execPath, ['--test', ...noesqlTests.map(file => `tests/${file}`)])
else blocked('noesql-reference-regressions', 'NoeSQL reference tests unavailable')

const seed = process.env.NOQERI_STAGE0
if (!seed) {
  blocked('trusted-bootstrap-seed', 'NOQERI_STAGE0 is not configured')
} else {
  const buildCommand = process.platform === 'win32'
    ? ['powershell', ['-NoProfile', '-File', 'scripts/build.ps1']]
    : ['sh', ['scripts/build.sh']]
  const built = run('build-selfhost', buildCommand[0], buildCommand[1], root, { NOQERI_STAGE0: seed })
  if (built) {
    run('stage2-abi', process.execPath, ['scripts/verify-stage2-module.mjs'])
    if (process.platform === 'win32') {
      blocked('core-conformance', 'POSIX-only test_core.sh must be ported or run on a POSIX validation target')
      blocked('historical-regressions', 'POSIX-only test.sh must be ported or run on a POSIX validation target')
    } else {
      run('core-conformance', 'sh', ['scripts/test_core.sh'], root, { NOQERI_RUNTIME_TESTS: '1' })
      run('historical-regressions', 'sh', ['scripts/test.sh'])
    }
    run('web-smoke', process.execPath, ['scripts/test_web_dev.mjs'])
    if (existsSync(resolve(root, 'Compiler/selfhost/bootstrap-local.json')))
      run('bootstrap-proof', process.execPath, ['scripts/bootstrap-stage2.mjs'])
    else blocked('bootstrap-proof', 'bootstrap-local.json missing')
    if (existsSync(resolve(root, 'build/noqeri')))
      run('game-sdk-type-check', resolve(root, 'build/noqeri'), ['check', 'tests/game_foundations.noqeri'])
    else blocked('game-sdk-type-check', 'verified compiler executable not present')
  } else {
    for (const name of ['stage2-abi', 'core-conformance', 'historical-regressions', 'web-smoke', 'bootstrap-proof', 'game-sdk-type-check'])
      blocked(name, 'blocked by failed bootstrap build')
  }
}

for (const [name, directory, file] of [
  ['registry-index', '../noqeri-registry', 'scripts/verify-index.mjs'],
  ['website-source-installer', '../noqeri-web', 'scripts/source-install.test.mjs']
]) {
  const cwd = resolve(root, directory)
  if (existsSync(resolve(cwd, file))) run(name, process.execPath, [file], cwd)
  else blocked(name, `repository or test missing: ${cwd}/${file}`)
}

const revision=spawnSync('git',['rev-parse','HEAD'],{cwd:root,encoding:'utf8'})
const sourceRevision=revision.status===0 && /^[0-9a-f]{40}$/.test(revision.stdout.trim())?revision.stdout.trim():null
const report = {
  schema: 2,
  version: '1.0.0',
  source_revision: sourceRevision,
  host: { platform: process.platform, architecture: process.arch },
  scope: 'general-purpose-language',
  generated_at: new Date().toISOString(),
  qualification: 'local-build',
  passed: steps.length > 0 && steps.every(step => step.passed),
  steps
}
writeFileSync(resolve(reportDir, 'report.json'), JSON.stringify(report, null, 2) + '\n')
console.log(`Report: ${resolve(reportDir, 'report.json')}`)
process.exitCode = report.passed ? 0 : 1
