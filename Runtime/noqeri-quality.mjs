#!/usr/bin/env node
import { existsSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { resolve } from 'node:path'

const raw = process.argv.slice(2)
if (raw.length === 0 || raw.includes('--help')) {
  console.log(`Noqeri quality driver\n\nExamples:\n  noqeri-quality run --check-memory app.noqeri\n  noqeri-quality test --race tests\n  noqeri-quality test --overflow tests\n\nHeavy diagnostics are opt-in. --check-memory and --race use instrumented host builds; --overflow enables checked Noqeri integer execution.`)
  process.exit(0)
}

let memory = false
let race = false
let overflow = false
const args = []
for (const arg of raw) {
  if (arg === '--check-memory') memory = true
  else if (arg === '--race') race = true
  else if (arg === '--overflow') overflow = true
  else args.push(arg)
}

if (memory && race) {
  console.error('noqeri-quality: --check-memory and --race use incompatible sanitizer runtimes; run them separately')
  process.exit(2)
}

const exe = process.platform === 'win32' ? 'noqeri.exe' : 'noqeri'
const normal = process.env.NOQERI_NATIVE || resolve('build', exe)
const memoryBin = process.env.NOQERI_MEMORY_CHECK_BINARY || resolve('build', 'memory', exe)
const raceBin = process.env.NOQERI_RACE_BINARY || resolve('build', 'race', exe)
const binary = memory ? memoryBin : race ? raceBin : normal

if (!existsSync(binary)) {
  const hint = memory
    ? 'cmake --preset memory-check && cmake --build --preset memory-check'
    : race
      ? 'cmake --preset race-check && cmake --build --preset race-check'
      : './scripts/build.sh'
  console.error(`noqeri-quality: ${binary} does not exist\nBuild it first with: ${hint}`)
  process.exit(2)
}

const env = { ...process.env }
if (overflow) env.NOQERI_CHECKED_OVERFLOW = '1'
if (memory) {
  env.ASAN_OPTIONS = env.ASAN_OPTIONS || 'abort_on_error=1:detect_leaks=1:strict_string_checks=1'
  env.UBSAN_OPTIONS = env.UBSAN_OPTIONS || 'halt_on_error=1:print_stacktrace=1'
}
if (race) env.TSAN_OPTIONS = env.TSAN_OPTIONS || 'halt_on_error=1:history_size=7'

const child = spawnSync(binary, args, { stdio: 'inherit', env })
if (child.error) {
  console.error(`noqeri-quality: ${child.error.message}`)
  process.exit(2)
}
process.exit(child.status ?? 2)
