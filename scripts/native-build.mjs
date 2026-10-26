#!/usr/bin/env node
import { createHash } from 'node:crypto'
import { copyFileSync, existsSync, mkdirSync, readFileSync, rmSync, statSync, writeFileSync, chmodSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { dirname, basename, resolve, relative } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..')

function fail(message) {
  console.error(`native-build: ${message}`)
  process.exit(2)
}

function option(name, fallback = '') {
  const prefix = `--${name}=`
  const value = process.argv.slice(2).find(arg => arg.startsWith(prefix))
  return value ? value.slice(prefix.length) : fallback
}

function sha256File(path) {
  return createHash('sha256').update(readFileSync(path)).digest('hex')
}

function run(command, args, cwd = root) {
  const result = spawnSync(command, args, {
    cwd,
    encoding: 'utf8',
    stdio: ['ignore', 'pipe', 'pipe'],
    maxBuffer: 32 * 1024 * 1024,
    env: { ...process.env, SOURCE_DATE_EPOCH: process.env.SOURCE_DATE_EPOCH || '0' }
  })
  if (result.error) fail(`could not launch ${command}: ${result.error.message}`)
  if (result.status !== 0) {
    if (result.stdout) process.stderr.write(result.stdout)
    if (result.stderr) process.stderr.write(result.stderr)
    fail(`${command} exited with status ${result.status}`)
  }
  return result
}

function version(command) {
  const result = spawnSync(command, ['--version'], {
    cwd: root,
    encoding: 'utf8',
    timeout: 5000,
    maxBuffer: 1024 * 1024
  })
  if (result.error || result.status !== 0) return null
  return `${result.stdout || result.stderr || ''}`.trim().split(/\r?\n/)[0] || null
}

const positional = process.argv.slice(2).filter(arg => !arg.startsWith('--'))
if (positional.length !== 2) {
  fail('usage: node scripts/native-build.mjs <source.noqeri> <output> [--compiler=build/noqeri] [--cxx=c++] [--opt=-O3] [--keep-asm] [--ai-framework] [--ai-lib=build/libnoqeri_ai.a] [--owned-collections] [--owned-collections-lib=build/libnoqeri_owned_collections.a]')
}
if (process.platform !== 'linux' || process.arch !== 'x64') {
  fail('qualified native AOT builds currently require Linux x86-64; other targets must not be labelled native-qualified yet')
}

const source = resolve(root, positional[0])
const output = resolve(root, positional[1])
const compiler = option('compiler', process.env.NOQERI_COMPILER || 'build/noqeri')
const compilerPath = compiler.includes('/') || compiler.includes('\\') ? resolve(root, compiler) : compiler
const cxx = option('cxx', process.env.CXX || 'c++')
const optimization = option('opt', '-O3')
const keepAsm = process.argv.includes('--keep-asm')
const linkAiFramework = process.argv.includes('--ai-framework')
const linkOwnedCollections = process.argv.includes('--owned-collections')
const ownedLibrary = resolve(root, option('owned-collections-lib', 'build/libnoqeri_owned_collections.a'))
const aiLib = resolve(root, option('ai-lib', 'build/libnoqeri_ai.a'))

if (!existsSync(source)) fail(`source does not exist: ${relative(root, source)}`)
if (!source.endsWith('.noqeri')) fail('source must use the .noqeri extension')
if (!/^-O(?:0|1|2|3|s|z)$/.test(optimization)) fail(`unsupported optimization flag: ${optimization}`)
if ((compilerPath.includes('/') || compilerPath.includes('\\')) && !existsSync(compilerPath)) {
  fail(`Noqeri compiler does not exist: ${compilerPath}`)
}

if (linkAiFramework && !existsSync(aiLib)) fail('native AI framework static library not found; build target noqeri_ai first or pass --ai-lib=PATH')
if (linkOwnedCollections && !existsSync(ownedLibrary)) fail('native collection library not found; build noqeri_owned_collections or pass --owned-collections-lib=PATH')

mkdirSync(dirname(output), { recursive: true })
const temp = resolve(dirname(output), `.${basename(output)}.native-tmp`)
rmSync(temp, { recursive: true, force: true })
mkdirSync(temp, { recursive: true })

const assembly = resolve(temp, 'program.s')
const programObject = resolve(temp, 'program.o')
const runnerObject = resolve(temp, 'runner.o')
const runnerSource = resolve(root, 'Runtime/native/aot_runner.cpp')

try {
  run(compilerPath, ['build', source, assembly, optimization])
  if (!existsSync(assembly) || statSync(assembly).size === 0) fail('Noqeri compiler did not emit assembly')

  const needsCompilerListRuntime = /\bcall\s+nq_list_(?:new|append|length|close|at|set)\b/.test(readFileSync(assembly, 'utf8'))
  const compilerListObject = resolve(temp, 'compiler-list-runtime.o')
  if (needsCompilerListRuntime && !linkOwnedCollections) {
    // A beginner's `let values = [5, 10]; values.append(15)` must not
    // require a separate build flag or a prebuilt static library.
    run(cxx, [
      '-std=c++17', '-O2', '-I', resolve(root, 'Include/noqeri'),
      '-c', resolve(root, 'Runtime/native/owned_collections.cpp'),
      '-o', compilerListObject
    ])
  }
  run(cxx, ['-c', assembly, '-o', programObject])
  run(cxx, [
    '-std=c++17',
    '-O2',
    '-I', resolve(root, 'Include/noqeri'),
    '-c', runnerSource,
    '-o', runnerObject
  ])
  run(cxx, [programObject, runnerObject, ...(linkAiFramework ? [aiLib] : []), ...(linkOwnedCollections ? [ownedLibrary] : needsCompilerListRuntime ? [compilerListObject] : []), '-o', output])

  if (!existsSync(output) || statSync(output).size === 0) fail('native executable was not produced')
  chmodSync(output, 0o755)
  if (keepAsm) copyFileSync(assembly, `${output}.s`)

  const provenance = {
    schema: 1,
    execution_model: 'native-aot',
    artifact_format: 'elf-executable',
    target: 'linux-x86_64-sysv',
    source: relative(root, source),
    source_sha256: sha256File(source),
    artifact: relative(root, output),
    artifact_sha256: sha256File(output),
    artifact_bytes: statSync(output).size,
    assembly_sha256: sha256File(assembly),
    assembly_artifact: keepAsm ? relative(root, `${output}.s`) : null,
    compiler: compiler,
    compiler_version: version(compilerPath),
    compiler_sha256: existsSync(compilerPath) ? sha256File(compilerPath) : null,
    compiler_provenance: 'stage0-cpp-native-backend',
    self_hosted_compiler: false,
    native_backend: 'Compiler/native_backend.cpp',
    host_adapter: 'Runtime/native/aot_runner.cpp',
    host_adapter_sha256: sha256File(runnerSource),
    cxx,
    cxx_version: version(cxx),
    optimization,
    ai_framework_linked: linkAiFramework,
    owned_collections_linked: linkOwnedCollections,
    owned_collections_library_sha256: linkOwnedCollections ? sha256File(ownedLibrary) : null,
    ai_framework_library_sha256: linkAiFramework ? sha256File(aiLib) : null
  }
  writeFileSync(`${output}.native.json`, JSON.stringify(provenance, null, 2) + '\n')
  console.log(`native-build: wrote ${relative(root, output)}`)
  console.log(`native-build: provenance ${relative(root, output)}.native.json`)
} finally {
  rmSync(temp, { recursive: true, force: true })
}
