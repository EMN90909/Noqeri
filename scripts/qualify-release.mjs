#!/usr/bin/env node
// Noqeri general-purpose release qualification. Independent game-engine
// certification belongs to scripts/check-game-sdk.mjs; game SDK tests are
// still included here to catch regressions in optional packages.
import { spawnSync } from 'node:child_process'
import { existsSync, mkdirSync, readdirSync, readFileSync, writeFileSync } from 'node:fs'
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
function passed(name, details = {}) {
  steps.push({ name, passed: true, ...details })
  console.log(`PASS ${name}`)
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
run('stage1-pin-metadata', process.execPath, ['--test', 'tests/bootstrap_seed_pin.test.mjs'])
const testsDir = resolve(root, 'tests')
const noesqlTests = existsSync(testsDir)
  ? readdirSync(testsDir).filter(file => /^noesql(?:-.*)?\.test\.mjs$/.test(file)).sort()
  : []
if (noesqlTests.length) run('noesql-reference-regressions', process.execPath, ['--test', ...noesqlTests.map(file => `tests/${file}`)])
else blocked('noesql-reference-regressions', 'NoeSQL reference tests unavailable')

const pinPath = resolve(root, 'Compiler/selfhost/stage1-seed.sha256')
const provenancePath = resolve(root, 'Compiler/selfhost/stage1-seed.json')
if (!existsSync(pinPath) || !existsSync(provenancePath)) {
  blocked('trusted-bootstrap-seed', 'repository Stage-1 SHA-256 pin/provenance is not configured')
} else {
  const pin = readFileSync(pinPath, 'utf8').trim().split(/\s+/)[0].toLowerCase()
  let provenance = null
  try { provenance = JSON.parse(readFileSync(provenancePath, 'utf8')) } catch {}
  if (!/^[0-9a-f]{64}$/.test(pin) || provenance?.seed_sha256 !== pin) {
    blocked('trusted-bootstrap-seed', 'repository Stage-1 pin metadata is invalid or inconsistent')
  } else {
    passed('trusted-bootstrap-seed', { sha256: pin, independently_attested: provenance.stage1_independently_trusted === true })
    const buildCommand = process.platform === 'win32'
      ? ['powershell', ['-NoProfile', '-File', 'scripts/build.ps1']]
      : ['sh', ['scripts/build.sh']]
    const built = run('build-nqri-selfhost', buildCommand[0], buildCommand[1])
    if (built) {
      const stage2Artifact = resolve(root, 'build/noqeri-stage2.nqri')
      const runtimeCandidates = process.platform === 'win32'
        ? [
            resolve(root, 'build/bootstrap-host/Release/noqeri-nqri.exe'),
            resolve(root, 'build/bootstrap-host/noqeri-nqri.exe'),
            resolve(root, 'build/noqeri-nqri.exe')
          ]
        : [
            resolve(root, 'build/bootstrap-host/noqeri-nqri'),
            resolve(root, 'build/bootstrap-host/Release/noqeri-nqri'),
            resolve(root, 'build/noqeri-nqri')
          ]
      const nqriRuntime = runtimeCandidates.find(candidate => existsSync(candidate))
      if (nqriRuntime && existsSync(stage2Artifact)) {
        run('stage2-nqri-abi', nqriRuntime, ['has-export', stage2Artifact, 'stage2CompileToNqri'])
      } else {
        blocked('stage2-nqri-abi', 'NQRI Stage-2 artifact or VM runtime is missing after bootstrap')
      }
      if (process.platform === 'win32') {
        blocked('core-conformance', 'POSIX-only test_core.sh must be ported or run on a POSIX validation target')
        blocked('historical-regressions', 'POSIX-only test.sh must be ported or run on a POSIX validation target')
      } else {
        run('core-conformance', 'sh', ['scripts/test_core.sh'], root, { NOQERI_RUNTIME_TESTS: '1' })
        run('historical-regressions', 'sh', ['scripts/test.sh'])
      }
      run('web-smoke', process.execPath, ['scripts/test_web_dev.mjs'])
      const textProof = resolve(root, 'build/nqri-selfhost-proof.txt')
      const jsonProof = resolve(root, 'build/nqri-selfhost-proof.json')
      let proofOk = false
      if (existsSync(textProof)) {
        const proof = readFileSync(textProof, 'utf8')
        proofOk = /(^|\n)stage1_pin_verified=true(\n|$)/.test(proof) &&
          /(^|\n)byte_equal=true(\n|$)/.test(proof) &&
          /(^|\n)equivalent=true(\n|$)/.test(proof) &&
          /(^|\n)execution_model=nqri-vm(\n|$)/.test(proof) &&
          /(^|\n)native_codegen=false(\n|$)/.test(proof) &&
          /(^|\n)native_selfhost=false(\n|$)/.test(proof)
      } else if (existsSync(jsonProof)) {
        try {
          const proof = JSON.parse(readFileSync(jsonProof, 'utf8'))
          proofOk = proof.stage1_pin_verified === true && proof.byte_equal === true && proof.equivalent === true &&
            proof.execution_model === 'nqri-vm' && proof.native_codegen === false && proof.native_selfhost === false
        } catch {}
      }
      if (proofOk) passed('nqri-bootstrap-proof')
      else blocked('nqri-bootstrap-proof', 'NQRI bootstrap proof is missing its pin/equality gates or incorrectly claims native code generation')
      if (existsSync(resolve(root, 'build/noqeri')))
        run('game-sdk-type-check', resolve(root, 'build/noqeri'), ['check', 'tests/game_foundations.noqeri'])
      else blocked('game-sdk-type-check', 'verified compiler executable not present')
    } else {
      for (const name of ['stage2-nqri-abi', 'core-conformance', 'historical-regressions', 'web-smoke', 'nqri-bootstrap-proof', 'game-sdk-type-check'])
        blocked(name, 'blocked by failed bootstrap build')
    }
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

const report = {
  schema: 2,
  version: '0.1.3',
  scope: 'general-purpose-language',
  generated_at: new Date().toISOString(),
  qualification: 'local-build',
  passed: steps.length > 0 && steps.every(step => step.passed),
  steps
}
writeFileSync(resolve(reportDir, 'report.json'), JSON.stringify(report, null, 2) + '\n')
console.log(`Report: ${resolve(reportDir, 'report.json')}`)
process.exitCode = report.passed ? 0 : 1
