import test from 'node:test'
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { copyFileSync, existsSync, mkdtempSync, mkdirSync, readFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { fileURLToPath } from 'node:url'

const source = fileURLToPath(new URL('../scripts/qualify-release.mjs', import.meta.url))
test('qualification runner parses as executable JavaScript', () => {
  const result = spawnSync(process.execPath, ['--check', source], { encoding: 'utf8' })
  assert.equal(result.status, 0, result.stderr)
})
test('missing compiler seed and required suites produce nonzero exit with machine-readable report', () => {
  const root = mkdtempSync(join(tmpdir(), 'noqeri-gate-'))
  try {
    mkdirSync(join(root, 'scripts'))
    copyFileSync(source, join(root, 'scripts', 'qualify-release.mjs'))
    const env = { ...process.env }
    delete env.NOQERI_STAGE0
    // Temporary project is intentionally missing compiler, web and database tests.
    const result = spawnSync(process.execPath, [join(root, 'scripts', 'qualify-release.mjs')], {
      cwd: root, env, encoding: 'utf8', timeout: 12000
    })
    assert.equal(result.status, 1, result.stderr)
    assert.ok(existsSync(join(root, 'build/release-qualification/report.json')))
    const report = JSON.parse(readFileSync(join(root, 'build/release-qualification/report.json'), 'utf8'))
    assert.equal(report.version, '0.7.0')
    assert.equal(report.scope, 'general-purpose-language')
    assert.equal(report.passed, false)
    assert.equal(report.steps.find(s => s.name === 'trusted-bootstrap-seed').passed, false)
    assert.equal(report.steps.find(s => s.name === 'noesql-reference-regressions').passed, false)
    assert.equal(report.steps.find(s => s.name === 'optional-game-asset-tests').passed, false)
  } finally {
    rmSync(root, { recursive: true, force: true })
  }
})
