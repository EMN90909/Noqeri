import test from 'node:test'
import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const repoRoot = resolve(fileURLToPath(new URL('..', import.meta.url)))
const compiler = process.env.NOQERI_TEST_COMPILER
const script = join(repoRoot, 'Tools', 'agent', 'loop.mjs')

function run(executable, args, cwd, input = '') {
  return new Promise((done, reject) => {
    const child = spawn(executable, args, { cwd, windowsHide: true })
    let stdout = '', stderr = ''
    child.stdout.on('data', chunk => { stdout += chunk.toString() })
    child.stderr.on('data', chunk => { stderr += chunk.toString() })
    child.on('error', reject)
    child.on('close', code => done({ code, stdout, stderr }))
    child.stdin.end(input)
  })
}

test('beginner diagnostics include code, position, source excerpt and repair hint', { skip: !compiler }, async () => {
  const root = await mkdtemp(join(tmpdir(), 'noqeri-diag-'))
  try {
    const file = join(root, 'main.noqeri')
    await writeFile(file, 'let result = missing\n')
    const human = await run(compiler, ['check', file], root)
    assert.notEqual(human.code, 0)
    assert.match(human.stderr, /error\[NQR-T3002\]/)
    assert.match(human.stderr, /main\.noqeri:1:14/)
    assert.match(human.stderr, /1 \| let result = missing/)
    assert.match(human.stderr, /\^/)
    assert.match(human.stderr, /help: Declare this name/)
    const machine = await run(compiler, ['check', file, '--json'], root)
    assert.notEqual(machine.code, 0)
    const data = JSON.parse(machine.stdout)
    assert.equal(data.schema, 1)
    assert.equal(data.ok, false)
    assert.equal(data.diagnostics[0].code, 'NQR-T3002')
    assert.equal(data.diagnostics[0].span.line, 1)
    assert.equal(data.diagnostics[0].span.column, 14)
    assert.match(data.diagnostics[0].help, /Declare this name/)
    await writeFile(file, 'print("Hello from Noqeri")\n')
    const fixed = await run(compiler, ['check', file, '--json'], root)
    assert.equal(fixed.code, 0)
    assert.deepEqual(JSON.parse(fixed.stdout).diagnostics, [])
  } finally { await rm(root, { recursive: true, force: true }) }
})

test('agent loop edits, diagnoses, retries, builds, stages, and rejects unsafe paths', { skip: !compiler }, async () => {
  const root = await mkdtemp(join(tmpdir(), 'noqeri-agent-'))
  try {
    const requests = [
      { id: 1, action: 'write_check', path: 'src/main.noqeri', content: 'let answer = missing\n' },
      { id: 2, action: 'read', path: 'src/main.noqeri' },
      { id: 3, action: 'write_check', path: 'src/main.noqeri', content: 'print("Hello from agent")\n' },
      { id: 4, action: 'check', path: '../escape.noqeri' },
      { id: 5, action: 'run', path: 'src/main.noqeri' },
      { id: 6, action: 'build', path: 'src/main.noqeri', format: 'nqri', output: 'out/main.nqri' },
      { id: 7, action: 'stage', artifact: 'out/main.nqri', output: 'dist/main.nqri' }
    ]
    const result = await run(process.execPath,
      [script, '--compiler', compiler, '--workspace', root, '--max-steps', '7'],
      repoRoot, requests.map(item => JSON.stringify(item)).join('\n') + '\n')
    assert.equal(result.code, 0, result.stderr)
    const replies = result.stdout.trim().split(/\r?\n/).map(JSON.parse)
    assert.equal(replies.length, 7)
    assert.equal(replies[0].ok, false)
    assert.equal(replies[0].diagnostics[0].code, 'NQR-T3002')
    assert.match(replies[1].content, /missing/)
    assert.equal(replies[2].ok, true)
    assert.equal(replies[3].ok, false)
    assert.match(replies[3].error, /workspace/)
    assert.equal(replies[4].ok, false)
    assert.match(replies[4].error, /--allow-run/)
    assert.equal(replies[5].ok, true, JSON.stringify(replies[5]))
    assert.equal(replies[6].ok, true, JSON.stringify(replies[6]))
    assert.match(replies[6].sha256, /^[a-f0-9]{64}$/)
    assert.ok((await readFile(join(root, 'dist', 'main.nqri'))).length > 0)
  } finally { await rm(root, { recursive: true, force: true }) }
})
