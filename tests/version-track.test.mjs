import test from 'node:test'
import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'

const read = file => readFile(new URL('../' + file, import.meta.url), 'utf8')

test('active public language and toolchain release fields agree on 0.1.4', async () => {
  const checks = [
    ['CMakeLists.txt', /project\(noqeri VERSION 0\.1\.4 LANGUAGES CXX\)/],
    ['Include/noqeri/version.hpp', /NOQERI_COMPILER_VERSION = "0\.1\.4"/],
    ['Include/noqeri/version.hpp', /NOQERI_LANGUAGE_VERSION = "0\.1\.4"/],
    ['project.nqon', /^version = "0\.1\.4"$/m],
    ['Programs/version.noqeri', /nqLanguageVersionMinor\(\): int \{ return 1 \}/],
    ['Programs/version.noqeri', /nqLanguageVersionPatch\(\): int \{ return 4 \}/],
    ['scripts/qualify-release.mjs', /version: '0\.1\.4'/],
    ['README.md', /Version on \x60main\x60: 0\.1\.4/]
  ]
  for (const [file, pattern] of checks)
    assert.match(await read(file), pattern, 'incorrect active version in ' + file)
  // This is a trust artifact, not the active public toolchain version.
  const historicalDriver = await read('Compiler/selfhost/driver.noqeri')
  assert.match(historicalDriver, /nqCompilerVersionMinor\(\): int \{ return 7 \}/)
  assert.match(historicalDriver, /nqCompilerVersionPatch\(\): int \{ return 0 \}/)
})
