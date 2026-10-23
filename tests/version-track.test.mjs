import test from 'node:test'
import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'

const read = file => readFile(new URL('../' + file, import.meta.url), 'utf8')

test('active public language/compiler version declarations are 0.1.43', async () => {
  const checks = [
    ['CMakeLists.txt', 'project(noqeri VERSION 0.1.43 LANGUAGES CXX)'],
    ['Include/noqeri/version.hpp', 'NOQERI_COMPILER_VERSION = "0.1.43"'],
    ['Include/noqeri/version.hpp', 'NOQERI_LANGUAGE_VERSION = "0.1.43"'],
    ['project.nqon', 'version = "0.1.43"'],
    ['Programs/version.noqeri', 'nqLanguageVersionMinor(): int { return 1 }'],
    ['Programs/version.noqeri', 'nqLanguageVersionPatch(): int { return 43 }'],
    ['scripts/qualify-release.mjs', "version: '0.1.43'"],
    ['README.md', 'Version on `main`: 0.1.43']
  ]
  for (const [file, expected] of checks) {
    const actual = await read(file)
    assert.ok(actual.includes(expected), 'incorrect active version in ' + file)
  }
  // Trusted Stage-1 is immutable bootstrap provenance, not a public toolchain tag.
  const historicalDriver = await read('Compiler/selfhost/driver.noqeri')
  assert.ok(historicalDriver.includes('nqCompilerVersionMinor(): int { return 7 }'))
  assert.ok(historicalDriver.includes('nqCompilerVersionPatch(): int { return 0 }'))
})
