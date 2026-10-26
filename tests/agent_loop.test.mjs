import test from 'node:test'
import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { mkdtemp, readFile, rm, writeFile, symlink } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import { createHash } from 'node:crypto'
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
    const alternate = await run(compiler, ['check', '--json', file], root)
    assert.equal(alternate.code, 1)
    assert.equal(JSON.parse(alternate.stdout).diagnostics[0].code, 'NQR-T3002')
    const badFlag = await run(compiler, ['check', file, '--json', '--definitely-unknown'], root)
    assert.equal(badFlag.code, 2)
    assert.equal(JSON.parse(badFlag.stdout).diagnostics[0].code, 'NQR-C0004')
    await writeFile(file, 'let foo = (1 + 2\n')
    const missingParen = await run(compiler, ['check', file, '--json'], root)
    assert.notEqual(missingParen.code, 0)
    assert.match(JSON.parse(missingParen.stdout).diagnostics[0].help, /close the opening/)
    await writeFile(file, 'let foo = ' + ' '.repeat(10000) + 'missing\n')
    const longLine = await run(compiler, ['check', file], root)
    assert.notEqual(longLine.code, 0)
    assert.ok(longLine.stderr.length < 3000, 'diagnostic excerpt must be bounded')
    assert.match(longLine.stderr, /missing/)
    await writeFile(file, 'print("Hello from Noqeri")\n')
    const fixed = await run(compiler, ['check', file, '--json'], root)
    assert.equal(fixed.code, 0)
    assert.deepEqual(JSON.parse(fixed.stdout).diagnostics, [])
  } finally { await rm(root, { recursive: true, force: true }) }
})

test('agent loop edits, diagnoses, retries, builds, stages, and rejects unsafe paths', { skip: !compiler }, async () => {
  const root = await mkdtemp(join(tmpdir(), 'noqeri-agent-'))
  try {
    const broken = 'let answer = missing\n'
    const revision = createHash('sha256').update(broken).digest('hex')
    const requests = [
      { id: 1, action: 'write_check', path: 'src/main.noqeri', content: broken },
      { id: 2, action: 'read', path: 'src/main.noqeri' },
      { id: 3, action: 'write_check', path: 'src/main.noqeri', content: 'print("Hello from agent")\n', expectedSha256: revision },
      { id: 4, action: 'check', path: '../escape.noqeri' },
      { id: 5, action: 'run', path: 'src/main.noqeri' },
      { id: 6, action: 'build', path: 'src/main.noqeri', format: 'nqri', output: 'out/main.nqri' },
      { id: 7, action: 'stage', artifact: 'out/main.nqri', output: 'dist/main.nqri' }
    ]
    const result = await run(process.execPath,
      [script, '--compiler', compiler, '--workspace', root, '--max-steps', '7',
        '--allow-write', '--allow-build', '--allow-stage'],
      repoRoot, requests.map(item => JSON.stringify(item)).join('\n') + '\n')
    assert.equal(result.code, 0, result.stderr)
    const replies = result.stdout.trim().split(/\r?\n/).map(JSON.parse)
    assert.equal(replies.length, 7)
    assert.equal(replies[0].ok, false)
    assert.equal(replies[0].diagnostics[0].code, 'NQR-T3002')
    assert.match(replies[1].content, /missing/)
    assert.equal(replies[1].sha256, revision)
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

test('write, build and stage are denied without explicit grants', async () => {
  const root = await mkdtemp(join(tmpdir(), 'noqeri-agent-deny-'))
  try {
    const requests = [
      { id: 1, action: 'write_check', path: 'src/main.noqeri', content: 'print(1)\n' },
      { id: 2, action: 'build', path: 'src/main.noqeri', format: 'nqri', output: 'out/main.nqri' },
      { id: 3, action: 'stage', artifact: 'out/main.nqri' },
      { id: 4, action: 'help' }
    ]
    const output = await run(process.execPath, [script, '--workspace', root, '--max-steps', '4'],
      repoRoot, requests.map(JSON.stringify).join('\n') + '\n')
    assert.equal(output.code, 0, output.stderr)
    const replies = output.stdout.trim().split(/\r?\n/).map(JSON.parse)
    assert.match(replies[0].error, /--allow-write/)
    assert.match(replies[1].error, /--allow-build/)
    assert.match(replies[2].error, /--allow-stage/)
    assert.equal(replies[3].allowWrite, false)
  } finally { await rm(root, { recursive: true, force: true }) }
})

test('stale revisions and workspace symlink escapes are rejected', async () => {
  const root = await mkdtemp(join(tmpdir(), 'noqeri-agent-revision-'))
  const external = await mkdtemp(join(tmpdir(), 'noqeri-agent-outside-'))
  try {
    await writeFile(join(root, 'main.noqeri'), 'print(1)\n')
    await writeFile(join(external, 'outside.noqeri'), 'print(123)\n')
    await symlink(external, join(root, 'linked'), 'dir')
    const requests = [
      { id: 1, action: 'write_check', path: 'main.noqeri', content: 'print(2)\n' },
      { id: 2, action: 'read', path: 'linked/outside.noqeri' },
      { id: 3, action: 'write_check', path: 'main.noqeri', content: 'print(3)\n',
        expectedSha256: '0'.repeat(64) }
    ]
    const output = await run(process.execPath, [script, '--workspace', root, '--max-steps', '3', '--allow-write'],
      repoRoot, requests.map(JSON.stringify).join('\n') + '\n')
    const replies = output.stdout.trim().split(/\r?\n/).map(JSON.parse)
    assert.equal(replies.length, 3)
    assert.match(replies[0].error, /revision mismatch/)
    assert.match(replies[1].error, /Symlinks/)
    assert.match(replies[2].error, /revision mismatch/)
    assert.equal(await readFile(join(root, 'main.noqeri'), 'utf8'), 'print(1)\n')
    assert.equal(await readFile(join(external, 'outside.noqeri'), 'utf8'), 'print(123)\n')
  } finally {
    await rm(root, { recursive: true, force: true })
    await rm(external, { recursive: true, force: true })
  }
})

test('staging requires this session build and refuses mutated artifacts', { skip: !compiler }, async () => {
  const root = await mkdtemp(join(tmpdir(), 'noqeri-agent-artifact-'))
  try {
    await writeFile(join(root, 'main.noqeri'), 'print(1)\n')
    await writeFile(join(root, 'old.nqri'), 'spoofed')
    const requests = [
      { id: 1, action: 'stage', artifact: 'old.nqri' },
      { id: 2, action: 'build', path: 'main.noqeri', format: 'nqri', output: 'out/main.nqri' },
      { id: 3, action: 'stage', artifact: 'out/main.nqri', output: 'dist/main.nqri' }
    ]
    const output = await run(process.execPath, [script, '--compiler', compiler, '--workspace', root,
      '--max-steps', '3', '--allow-build', '--allow-stage'],
      repoRoot, requests.map(JSON.stringify).join('\n') + '\n')
    const replies = output.stdout.trim().split(/\r?\n/).map(JSON.parse)
    assert.match(replies[0].error, /successful build/)
    assert.equal(replies[1].ok, true, JSON.stringify(replies[1]))
    assert.equal(replies[2].ok, true, JSON.stringify(replies[2]))
    assert.equal(replies[2].sha256, replies[1].sha256)
  } finally { await rm(root, { recursive: true, force: true }) }
})
