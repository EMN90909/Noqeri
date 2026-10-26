#!/usr/bin/env node
// A small, offline JSON-lines development loop over the existing Stage-0 compiler.
// It does not call an LLM, run a shell, or deploy to an external service.
import { spawn } from 'node:child_process'
import { createHash, randomUUID } from 'node:crypto'
import { createInterface } from 'node:readline'
import { copyFile, lstat, mkdir, readFile, realpath, rename, rm, writeFile } from 'node:fs/promises'
import { basename, dirname, extname, isAbsolute, join, relative, resolve, sep } from 'node:path'

const MAX_SOURCE_BYTES = 256 * 1024
const MAX_OUTPUT_BYTES = 128 * 1024
const MAX_LINE_BYTES = 512 * 1024
const TIMEOUT_MS = 15000
const options = { compiler: process.env.NOQERI_BIN || 'noqeri', workspace: '.', maxSteps: 12, allowRun: false }

function usage() {
  return 'node Tools/agent/loop.mjs --compiler ./build/noqeri --workspace ./project [--max-steps 12] [--allow-run]'
}
for (let i = 2; i < process.argv.length; i++) {
  const option = process.argv[i]
  if (option === '--compiler' || option === '--workspace' || option === '--max-steps') {
    if (++i >= process.argv.length) throw new Error(option + ' requires a value')
    options[{ '--compiler': 'compiler', '--workspace': 'workspace', '--max-steps': 'maxSteps' }[option]] = process.argv[i]
  } else if (option === '--allow-run') options.allowRun = true
  else if (option === '--help') { console.log(usage()); process.exit(0) }
  else throw new Error('Unknown option: ' + option + '. ' + usage())
}
options.maxSteps = Number(options.maxSteps)
if (!Number.isSafeInteger(options.maxSteps) || options.maxSteps < 1 || options.maxSteps > 64) {
  throw new Error('--max-steps must be between 1 and 64')
}
const root = await realpath(resolve(options.workspace))
const compiler = options.compiler.includes('/') || options.compiler.includes('\\')
  ? resolve(options.compiler) : options.compiler

// Paths stay under the selected workspace. Reject symlinks, including directory
// components, to avoid accidentally following a link outside it.
async function workspacePath(candidate, suffixes) {
  if (typeof candidate !== 'string' || !candidate || candidate.includes('\0') ||
      isAbsolute(candidate) || /^[a-zA-Z]:/.test(candidate)) throw new Error('Expected a relative workspace path')
  const absolute = resolve(root, candidate)
  const rel = relative(root, absolute)
  if (!rel || rel === '..' || rel.startsWith('..' + sep) || isAbsolute(rel)) {
    throw new Error('Path must remain inside the workspace')
  }
  if (suffixes && !suffixes.includes(extname(absolute))) {
    throw new Error('Expected a ' + suffixes.join(' or ') + ' file')
  }
  let cursor = root
  for (const segment of rel.split(sep)) {
    cursor = join(cursor, segment)
    try {
      if ((await lstat(cursor)).isSymbolicLink()) throw new Error('Symlinks are not allowed: ' + candidate)
    } catch (error) {
      if (error.code !== 'ENOENT') throw error
    }
  }
  return absolute
}

async function execute(args) {
  return await new Promise((resolveResult) => {
    const child = spawn(compiler, args, { cwd: root, shell: false, windowsHide: true })
    let stdout = '', stderr = '', truncated = false, timedOut = false, settled = false
    const append = (key, data) => {
      const chunk = data.toString('utf8')
      const current = key === 'stdout' ? stdout : stderr
      if (Buffer.byteLength(current) + Buffer.byteLength(chunk) > MAX_OUTPUT_BYTES) {
        truncated = true
        child.kill()
      } else if (key === 'stdout') stdout += chunk
      else stderr += chunk
    }
    child.stdout?.on('data', chunk => append('stdout', chunk))
    child.stderr?.on('data', chunk => append('stderr', chunk))
    const timer = setTimeout(() => { timedOut = true; child.kill() }, TIMEOUT_MS)
    const finish = (exitCode, error = null) => {
      if (settled) return
      settled = true
      clearTimeout(timer)
      resolveResult({ exitCode, stdout, stderr, timedOut, truncated, ...(error && { error }) })
    }
    child.on('error', error => finish(null, error.message))
    child.on('close', code => finish(code))
  })
}

async function check(source) {
  const path = await workspacePath(source, ['.noqeri'])
  const result = await execute(['check', path, '--json'])
  let report = null
  try { report = JSON.parse(result.stdout) } catch { /* missing or invalid compiler protocol */ }
  if (!report || report.schema !== 1 || typeof report.ok !== 'boolean' ||
      !Array.isArray(report.diagnostics)) {
    return { ok: false, error: 'Compiler did not return valid diagnostics JSON', ...result }
  }
  return { ok: result.exitCode === 0 && report.ok && !result.timedOut && !result.truncated,
    diagnostics: report.diagnostics, modules: report.modules ?? null,
    exitCode: result.exitCode, timedOut: result.timedOut, truncated: result.truncated,
    ...(result.stderr && { stderr: result.stderr }) }
}

async function writeSource(path, content) {
  const absolute = await workspacePath(path, ['.noqeri'])
  if (typeof content !== 'string' || Buffer.byteLength(content) > MAX_SOURCE_BYTES) {
    throw new Error('Source must be a string of at most 256 KiB')
  }
  await mkdir(dirname(absolute), { recursive: true })
  const temp = join(dirname(absolute), '.' + basename(absolute) + '.' + randomUUID() + '.tmp')
  try {
    await writeFile(temp, content, { flag: 'wx', mode: 0o600 })
    await rename(temp, absolute)
  } finally {
    await rm(temp, { force: true })
  }
  return check(path)
}

async function handle(request) {
  if (!request || typeof request !== 'object' || Array.isArray(request)) {
    throw new Error('Expected an object with an action')
  }
  switch (request.action) {
    case 'help':
      return { ok: true, protocol: 1, actions: ['help', 'read', 'write_check', 'check', 'build', 'stage', 'run'],
        maxSteps: options.maxSteps, allowRun: options.allowRun,
        notes: 'Each request gets one JSON response. build produces assembly or NQRI; stage copies the artifact into dist/. No remote deploy.' }
    case 'read': {
      const file = await workspacePath(request.path, ['.noqeri'])
      const data = await readFile(file)
      if (data.length > MAX_SOURCE_BYTES) throw new Error('Source is larger than 256 KiB')
      return { ok: true, path: request.path, content: data.toString('utf8') }
    }
    case 'write_check':
      return await writeSource(request.path, request.content)
    case 'check':
      return await check(request.path)
    case 'build': {
      const source = await workspacePath(request.path, ['.noqeri'])
      const format = request.format || 'nqri'
      if (!['nqri', 'assembly'].includes(format)) throw new Error('format must be nqri or assembly')
      const suffix = format === 'nqri' ? '.nqri' : '.s'
      const output = await workspacePath(request.output, [suffix])
      await mkdir(dirname(output), { recursive: true })
      // Remove a previous artifact so a failed build cannot appear successful.
      await rm(output, { force: true })
      const result = await execute([format === 'nqri' ? 'nqri' : 'build', source, output])
      return { ok: result.exitCode === 0 && !result.timedOut && !result.truncated,
        format, artifact: request.output, ...result }
    }
    case 'stage': {
      const source = await workspacePath(request.artifact, ['.nqri', '.s'])
      const outputName = request.output || ('dist/' + basename(source))
      if (typeof outputName !== 'string' || !outputName.replace(/\\/g, '/').startsWith('dist/')) {
        throw new Error('Staged artifacts must be placed under dist/')
      }
      const output = await workspacePath(outputName, [extname(source)])
      const normalizedOutput = relative(root, output).split(sep).join('/')
      if (!normalizedOutput.startsWith('dist/')) throw new Error('Staged artifacts must remain under dist/')
      if (source === output) throw new Error('Staging output must differ from the input')
      const bytes = await readFile(source)
      await mkdir(dirname(output), { recursive: true })
      await copyFile(source, output)
      return { ok: true, artifact: outputName, bytes: bytes.length,
        sha256: createHash('sha256').update(bytes).digest('hex'),
        note: 'Local staging only; deploy or link using an explicit platform tool.' }
    }
    case 'run': {
      if (!options.allowRun) throw new Error('Executing programs requires --allow-run')
      const source = await workspacePath(request.path, ['.noqeri'])
      const result = await execute(['run', source])
      return { ok: result.exitCode === 0 && !result.timedOut && !result.truncated, ...result }
    }
    default: throw new Error('Unknown action; send {"action":"help"} to see the protocol')
  }
}

const rl = createInterface({ input: process.stdin, crlfDelay: Infinity })
let steps = 0
for await (const line of rl) {
  if (!line.trim()) continue
  let request
  try {
    if (Buffer.byteLength(line) > MAX_LINE_BYTES) throw new Error('Request exceeds 512 KiB')
    request = JSON.parse(line)
    if (++steps > options.maxSteps) throw new Error('Step limit reached; restart explicitly for another bounded session')
    const result = await handle(request)
    console.log(JSON.stringify({ id: request?.id ?? null, ...result }))
  } catch (error) {
    console.log(JSON.stringify({ id: request?.id ?? null, ok: false, error: error.message }))
  }
  if (steps >= options.maxSteps) break
}
