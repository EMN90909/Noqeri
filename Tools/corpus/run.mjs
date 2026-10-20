#!/usr/bin/env node
import { existsSync, mkdirSync, readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs'
import { dirname, join, relative, resolve } from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const root = resolve(fileURLToPath(new URL('../..', import.meta.url)))
const args = process.argv.slice(2)
const option = name => args.find(arg => arg.startsWith(`--${name}=`))?.slice(name.length + 3)
const requireComplete = args.includes('--require-complete')
const manifestPath = resolve(option('manifest') || join(root, 'tests', 'corpus', 'corpus.json'))
const outputPath = resolve(option('output') || join(root, 'build', 'corpus-results.json'))
const candidateBins = [join(root, 'build', 'noqeri'), join(root, 'build', 'Release', 'noqeri.exe'), join(root, 'build', 'release', 'noqeri')]
const noqeri = resolve(option('noqeri') || candidateBins.find(existsSync) || candidateBins[0])

function usage() {
  console.log(`Noqeri practical corpus runner\n\nUsage:\n  node Tools/corpus/run.mjs [--noqeri=path] [--output=path] [--require-complete]\n\nWithout --require-complete, active entries must pass and pending entries remain visible. A full-release corpus claim must use --require-complete.`)
}
if (args.includes('--help') || args.includes('-h')) { usage(); process.exit(0) }
if (!existsSync(noqeri)) { console.error(`corpus: compiler not found: ${noqeri}`); process.exit(2) }

const corpus = JSON.parse(readFileSync(manifestPath, 'utf8'))
const required = new Set(corpus.policy?.required_classes || [])
const entries = Array.isArray(corpus.entries) ? corpus.entries : []

function normalize(text) { return String(text || '').replace(/\r\n/g, '\n') }
function nqrFiles(path) {
  const result = []
  if (!existsSync(path)) return result
  for (const name of readdirSync(path)) {
    const full = join(path, name)
    const info = statSync(full)
    if (info.isDirectory()) result.push(...noqeriFiles(full))
    else if (info.isFile() && name.endsWith('.noqeri')) result.push(full)
  }
  return result
}
function sourcePolicy(entry, cwd) {
  const forbidden = entry.source_policy?.forbid || []
  const violations = []
  if (!forbidden.length) return violations
  for (const file of nqrFiles(cwd)) {
    const text = readFileSync(file, 'utf8')
    for (const pattern of forbidden) {
      if (text.includes(pattern)) violations.push(`${relative(root, file)} contains forbidden token ${JSON.stringify(pattern)}`)
    }
  }
  return violations
}
function execute(check, cwd) {
  const started = process.hrtime.bigint()
  const result = spawnSync(noqeri, check.args || [], { cwd, encoding: 'utf8', timeout: 60_000, maxBuffer: 4 * 1024 * 1024 })
  const duration = Number(process.hrtime.bigint() - started) / 1e6
  const stdout = normalize(result.stdout), stderr = normalize(result.stderr)
  const failures = []
  if (result.error) failures.push(result.error.message)
  if (result.status !== Number(check.expect_exit ?? 0)) failures.push(`exit=${result.status} expected=${check.expect_exit ?? 0}`)
  if (check.expect_stdout != null && stdout !== normalize(check.expect_stdout)) failures.push(`stdout mismatch: got ${JSON.stringify(stdout)}`)
  if (check.expect_stdout_contains != null && !stdout.includes(check.expect_stdout_contains)) failures.push(`stdout missing ${JSON.stringify(check.expect_stdout_contains)}`)
  if (check.expect_stderr_contains != null && !stderr.includes(check.expect_stderr_contains)) failures.push(`stderr missing ${JSON.stringify(check.expect_stderr_contains)}`)
  return {
    name: check.name || (check.args || []).join(' '),
    command: [noqeri, ...(check.args || [])],
    status: failures.length ? 'failed' : 'passed',
    exit_code: result.status,
    signal: result.signal || null,
    duration_ms: Math.round(duration * 1000) / 1000,
    stdout_tail: stdout.slice(-4000),
    stderr_tail: stderr.slice(-4000),
    failures
  }
}

const results = []
for (const entry of entries) {
  if (entry.status !== 'active') {
    results.push({ id: entry.id, class: entry.class, status: 'pending', blocker: entry.blocker || 'not activated' })
    continue
  }
  const cwd = resolve(root, entry.path)
  const failures = []
  if (!existsSync(cwd)) failures.push(`entry path missing: ${entry.path}`)
  if (!entry.target) failures.push('target missing')
  if (!entry.profile) failures.push('profile missing')
  if (!entry.behavior) failures.push('behavior missing')
  const policyViolations = existsSync(cwd) ? sourcePolicy(entry, cwd) : []
  failures.push(...policyViolations)
  const checks = []
  if (!Array.isArray(entry.checks) || entry.checks.length === 0) failures.push('no executable checks declared')
  else if (failures.length === 0) {
    for (const check of entry.checks) checks.push(execute(check, cwd))
    for (const check of checks) if (check.status !== 'passed') failures.push(`${check.name} failed`)
  }
  results.push({
    id: entry.id,
    class: entry.class,
    status: failures.length ? 'failed' : 'passed',
    target: entry.target,
    profile: entry.profile,
    path: entry.path,
    source_policy_violations: policyViolations,
    checks,
    failures
  })
}

const represented = new Set(entries.map(entry => entry.class))
const missingClasses = [...required].filter(name => !represented.has(name)).sort()
const pendingClasses = results.filter(result => result.status === 'pending').map(result => result.class).sort()
const failed = results.filter(result => result.status === 'failed')
const passed = results.filter(result => result.status === 'passed')
const report = {
  schema: 1,
  generated_at: new Date().toISOString(),
  compiler: noqeri,
  compiler_probe: execute({ name: 'compiler-version', args: ['--version'], expect_exit: 0 }, root),
  require_complete: requireComplete,
  summary: { active_passed: passed.length, active_failed: failed.length, pending: pendingClasses.length, missing_required_classes: missingClasses.length },
  pending_classes: pendingClasses,
  missing_required_classes: missingClasses,
  entries: results
}
mkdirSync(dirname(outputPath), { recursive: true })
writeFileSync(outputPath, JSON.stringify(report, null, 2) + '\n')
console.log(`corpus: active-pass=${passed.length} active-fail=${failed.length} pending=${pendingClasses.length} missing=${missingClasses.length}`)
for (const item of results) console.log(`${item.status.padEnd(7)} ${item.class} ${item.id}${item.blocker ? ` — ${item.blocker}` : ''}`)
console.log(`evidence -> ${outputPath}`)

if (failed.length || missingClasses.length || (requireComplete && pendingClasses.length)) process.exitCode = 1
