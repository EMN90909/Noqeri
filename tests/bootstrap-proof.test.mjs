import test from 'node:test'
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { mkdtempSync, mkdirSync, writeFileSync, readFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { fileURLToPath } from 'node:url'

const verifier = fileURLToPath(new URL('../scripts/bootstrap-stage2.mjs', import.meta.url))
function fixture() {
  const root = mkdtempSync(join(tmpdir(), 'noqeri-bootstrap-'))
  mkdirSync(join(root, 'Compiler/selfhost'), { recursive: true })
  mkdirSync(join(root, 'build/bootstrap'), { recursive: true })
  writeFileSync(join(root, 'Compiler/selfhost/compiler-sources.txt'), 'Compiler/selfhost/main.nqr\n')
  writeFileSync(join(root, 'Compiler/selfhost/main.nqr'), 'module test\n')
  for (const name of ['stage1', 'stage2', 'rebuilt']) writeFileSync(join(root, 'build/bootstrap', name), 'identical contents')
  return root
}
function runVerifier(root, config) {
  writeFileSync(join(root, 'Compiler/selfhost/bootstrap-local.json'), JSON.stringify(config))
  const result = spawnSync(process.execPath, [verifier, `--root=${root}`], { cwd: root, encoding: 'utf8', timeout: 10000 })
  const proof = JSON.parse(readFileSync(join(root, 'build/stage2-proof.json'), 'utf8'))
  return { result, proof }
}
function config() {
  const ok = [process.execPath, '-e', 'process.exit(0)']
  return {
    stage0_built_stage1: true, release_needs_cpp: false,
    artifacts: { stage1: 'build/bootstrap/stage1', stage2: 'build/bootstrap/stage2', rebuilt: 'build/bootstrap/rebuilt' },
    commands: { stage0_builds_stage1: null, stage1_builds_stage2: ok, stage2_rebuilds_compiler: ok },
    suites: Object.fromEntries(['frontend', 'nir', 'optimizer', 'object_writers', 'formatter', 'lsp', 'package_manager', 'fuzz', 'race', 'memory', 'overflow', 'reproducible_build', 'practical_corpus'].map(name => [name, ok]))
  }
}
test('missing configured Stage-0 run cannot be certified by a boolean and dummy artifacts', () => {
  const root = fixture()
  try {
    const { result, proof } = runVerifier(root, config())
    assert.equal(result.status, 1, result.stderr)
    assert.equal(proof.verified, false)
    assert.equal(proof.stage0_proven, false)
    assert.equal(proof.stage1_built_stage2, false)
  } finally { rmSync(root, { recursive: true, force: true }) }
})
test('identical compiler artifact paths cannot be certified as a genuine rebuild', () => {
  const root = fixture()
  try {
    const input = config()
    input.commands.stage0_builds_stage1 = [process.execPath, '-e', 'require("node:fs").writeFileSync("build/bootstrap/stage1", "fresh stage1 artifact")']
    input.artifacts.rebuilt = input.artifacts.stage2
    const { result, proof } = runVerifier(root, input)
    assert.equal(result.status, 1, result.stderr)
    assert.equal(proof.verified, false)
    assert.equal(proof.stage0_proven, true)
    assert.equal(proof.artifacts_distinct, false)
    assert.equal(proof.outputs_equivalent, false)
  } finally { rmSync(root, { recursive: true, force: true }) }
})
test('stale stage-1 artifact cannot establish provenance', () => {
  const root = fixture()
  try {
    const input = config()
    input.commands.stage0_builds_stage1 = [process.execPath, '-e', 'process.exit(0)']
    const { result, proof } = runVerifier(root, input)
    assert.equal(result.status, 1, result.stderr)
    assert.equal(proof.stage0_proven, false)
    assert.equal(proof.verified, false)
  } finally { rmSync(root, { recursive: true, force: true }) }
})
test('bootstrap verifier script has valid Node.js syntax', () => {
  const result = spawnSync(process.execPath, ['--check', verifier], { encoding: 'utf8' })
  assert.equal(result.status, 0, result.stderr)
})
