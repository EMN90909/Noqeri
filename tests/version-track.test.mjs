import test from 'node:test'
import assert from 'node:assert/strict'
import {readFile} from 'node:fs/promises'

const read = async file => readFile(new URL('../'+file,import.meta.url),'utf8')
test('active language/compiler release declarations all agree on 0.1.3',async()=>{
  const checks = [
    ['CMakeLists.txt',/project\(noqeri VERSION 0\.1\.3 LANGUAGES CXX\)/],
    ['Include/noqeri/version.hpp',/NOQERI_COMPILER_VERSION = "0\.1\.3"/],
    ['Include/noqeri/version.hpp',/NOQERI_LANGUAGE_VERSION = "0\.1\.3"/],
    ['project.nqon',/^version = "0\.1\.3"$/m],
    ['Compiler/selfhost/driver.noqeri',/nqCompilerVersionMinor\(\): int \{ return 1 \}/],
    ['Compiler/selfhost/driver.noqeri',/nqCompilerVersionPatch\(\): int \{ return 3 \}/],
    ['Programs/version.noqeri',/nqLanguageVersionMinor\(\): int \{ return 1 \}/],
    ['Programs/version.noqeri',/nqLanguageVersionPatch\(\): int \{ return 3 \}/],
    ['scripts/qualify-release.mjs',/version: '0\.1\.3'/],
    ['README.md',/Version on `main`: 0\.1\.3/]
  ]
  // The archived, checksum-pinned Stage-1 seed is a trust artifact and stays
  // at the historical 0.7.0 version until a verified seed rotation.
  const bootstrap = await read('Compiler/selfhost/driver.noqeri')
  assert.match(bootstrap, /nqCompilerVersionMinor\\(\\): int \\{ return 7 \\}/)
  for(const [file,pattern] of checks)
    assert.match(await read(file),pattern,'incorrect active version in '+file)
})
