#!/usr/bin/env node
import { mkdtempSync, mkdirSync, writeFileSync, rmSync, existsSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import { spawnSync } from 'node:child_process'

const opts = Object.fromEntries(process.argv.slice(2).map(x => {
  const [k, v = 'true'] = x.replace(/^--/, '').split('=')
  return [k, v]
}))
const cases = Math.max(1, Number(opts.cases || 250))
const seed = Number(opts.seed || 0x4e515246) >>> 0
const timeout = Math.max(50, Number(opts.timeout || 1500))
const exe = process.platform === 'win32' ? 'noqeri.exe' : 'noqeri'
const noqeri = process.env.NOQERI_NATIVE || resolve('build', exe)
if (!existsSync(noqeri)) {
  console.error(`fuzz-all: ${noqeri} is missing; build Noqeri first`)
  process.exit(2)
}

let state = seed || 1
function rnd() { state ^= state << 13; state ^= state >>> 17; state ^= state << 5; return state >>> 0 }
function pick(xs) { return xs[rnd() % xs.length] }
function bytes(max = 768) {
  const n = rnd() % max
  const out = Buffer.alloc(n)
  for (let i = 0; i < n; i++) out[i] = rnd() & 0xff
  return out
}
function nonEmptyBytes(max = 768) {
  const value = bytes(max)
  return value.length ? value : Buffer.from([0])
}
function mutateText(seedText) {
  const alphabet = '(){}[]<>+-*/%=!&|,:;.abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_ \n\t"\\'
  let s = seedText
  const edits = 1 + (rnd() % 24)
  for (let e = 0; e < edits; e++) {
    const at = s.length ? rnd() % s.length : 0
    const op = rnd() % 4
    if (op === 0) s = s.slice(0, at) + pick(alphabet) + s.slice(at)
    else if (op === 1 && s.length) s = s.slice(0, at) + s.slice(at + 1)
    else if (op === 2 && s.length) s = s.slice(0, at) + pick(alphabet) + s.slice(at + 1)
    else s = s.slice(0, at) + pick(['function','record','import','let','while','return','unsafe','null','0','999999999999999999999']) + s.slice(at)
  }
  return s
}
function byteArrayLiteral(buffer) {
  return [...buffer].map(x => `${x} as u8`).join(',')
}
function parserHarness(buffer) {
  const literal = byteArrayLiteral(buffer)
  return `import "../../Lib/std/sql.noqeri"\nimport "../../Lib/std/url.noqeri"\nimport "../../Lib/std/http.noqeri"\nimport "../../Lib/std/dns.noqeri"\n\nlet data: [u8; ${buffer.length}] = [${literal}]\nlet view: []u8 = slice(data)\nlet decoded: [u8; ${Math.max(8, buffer.length * 3 + 8)}]\nsql_count_placeholders(view)\nsql_has_unterminated_quote(view)\nsql_identifier_valid(view)\nurl_has_scheme(view)\nurl_percent_encoded_len(view)\nurl_percent_decode(view, slice(decoded))\ndns_name_valid(view)\nlet i: usize = 0 as usize\nwhile i < len(view) { http_is_token_char(view[i]) i = i + 1 as usize }\nprint(0)\n`
}
function formatAwareObject() {
  const out = nonEmptyBytes(1024)
  const kind = rnd() % 4
  if (kind === 0 && out.length >= 4) Buffer.from([0x7f, 0x45, 0x4c, 0x46]).copy(out, 0) // ELF
  else if (kind === 1 && out.length >= 4) Buffer.from([0xcf, 0xfa, 0xed, 0xfe]).copy(out, 0) // Mach-O 64 little endian
  else if (kind === 2 && out.length >= 2) Buffer.from([0x64, 0x86]).copy(out, 0) // COFF x86-64 machine
  return out
}

const root = mkdtempSync(join(tmpdir(), 'noqeri-fuzz-'))
const generatedRoot = resolve('build', 'fuzz-generated')
mkdirSync(generatedRoot, { recursive: true })
const crashes = []
function run(label, args, expected = new Set([0, 1])) {
  const r = spawnSync(noqeri, args, { encoding: 'utf8', timeout, maxBuffer: 2 * 1024 * 1024 })
  const timedOut = r.error?.code === 'ETIMEDOUT'
  const signalCrash = r.signal && !['SIGTERM'].includes(r.signal)
  const badExit = r.status !== null && !expected.has(r.status)
  if (timedOut || signalCrash || badExit || r.error && !timedOut) {
    crashes.push({ label, args, status: r.status, signal: r.signal, error: r.error?.message || '', stderr: (r.stderr || '').slice(0, 1200) })
    return false
  }
  return true
}

const sourceSeed = 'module fuzz.seed\nrecord Pair { left: int, right: int }\nfunction add(a: int, b: int): int { return a + b }\nlet x: int = add(1, 2)\nprint(x)\n'
const nqdSeed = 'table users { id: int key, name: text required }\ndelete users\ninsert users { id: 1, name: "Ada" }\nselect users\n'
const manifestSeed = 'package { name: "fuzz/project" version: "0.0.1" edition: "2026" entry: "main.noqeri" profile: "application" target: "x86_64-unknown-none" }\n'

try {
  for (let i = 0; i < cases; i++) {
    const source = join(root, `case-${i}.noqeri`)
    writeFileSync(source, mutateText(sourceSeed))
    run('lexer', ['lex', source])
    run('parser+type-checker+borrow+safety', ['check', source])
    run('lowerer', ['nir', source])

    const nqd = join(root, `case-${i}.nqd`)
    const db = join(root, `case-${i}.nqdb`)
    const nqdText = i % 3 === 0 ? mutateText(nqdSeed) : bytes(512).toString('latin1')
    writeFileSync(nqd, nqdText)
    run('database/.nqd parser', ['db', nqd, db])

    const manifest = join(root, `project-${i}.noqeri`)
    writeFileSync(manifest, mutateText(manifestSeed))
    run('package manifest parser', ['manifest', manifest])

    // Directly exercise library parsers instead of assuming compiler fuzzing
    // covers protocol input. The generated file lives under build/ so relative
    // imports resolve against the checked-out repository.
    const parserInput = i % 2 === 0 ? Buffer.from(mutateText("SELECT * FROM users WHERE name = '?' https://example.test/a%20b"), 'utf8') : nonEmptyBytes(384)
    const parserSource = join(generatedRoot, `parsers-${i}.noqeri`)
    writeFileSync(parserSource, parserHarness(parserInput))
    run('sql+url+dns+http parsers', ['run', parserSource])

    const object = join(root, `object-${i}.o`)
    writeFileSync(object, formatAwareObject())
    // The loader/linker may reject arbitrary objects, but must not crash/hang.
    run('object-loader+linker', ['link', join(root, `out-${i}`), object])

    if (crashes.length) break
  }

  if (crashes.length) {
    const report = join(process.cwd(), 'build', 'fuzz-crash.json')
    try { writeFileSync(report, JSON.stringify({ seed, cases, crashes }, null, 2)) } catch {}
    console.error(JSON.stringify({ result: 'crash', seed, crashes }, null, 2))
    process.exitCode = 1
  } else {
    console.log(JSON.stringify({ result: 'ok', seed, cases, targets: ['lexer','parser/type-checker/borrow/safety','lowerer','database/.nqd','package-manifest','sql/url/dns/http','object-loader/linker'] }))
  }
} finally {
  rmSync(root, { recursive: true, force: true })
  rmSync(generatedRoot, { recursive: true, force: true })
}
