import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtemp, writeFile, readFile, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { spawnSync } from 'node:child_process'

const compiler = process.env.NOQERI_PGAS_COMPILER || '/tmp/noqeri-pgas-build/noqeri'

test('Stage-0 validates partitions, emits native distributed accesses and rejects local-only execution', async () => {
  const dir = await mkdtemp(join(tmpdir(), 'pgas-attr-'))
  try {
    const good = join(dir, 'typed.noqeri')
    await writeFile(good, '@partition(strategy: "block", nodes: 2) let dataset: [i64; 16]\n' +
      'dataset[1] = 7 as i64\nprint(dataset[1])\n')
    const invoke = (command, file = good, ...args) =>
      spawnSync(compiler, [command, file, ...args], { encoding: 'utf8', timeout: 12000 })

    const check = invoke('check')
    assert.equal(check.status, 0, check.stderr)
    const nir = invoke('nir')
    assert.equal(nir.status, 0, nir.stderr)
    assert.match(nir.stdout, /@partition dataset strategy=block nodes=2 elements=16/)
    assert.match(nir.stdout, /call nq_pgas_compiler_partition_read/)
    assert.match(nir.stdout, /call nq_pgas_compiler_partition_write/)

    const run = invoke('run')
    assert.notEqual(run.status, 0, 'interpreter must never treat a remote partition as local storage')
    assert.match(run.stderr, /PGAS partitions require a distributed backend/)
    const assembly = join(dir, 'native-remote.s')
    const build = invoke('build', good, assembly, '-O0')
    assert.equal(build.status, 0, build.stderr)
    const emitted = await readFile(assembly, 'utf8')
    assert.match(emitted, /call nq_pgas_compiler_partition_read/)
    assert.match(emitted, /call nq_pgas_compiler_partition_write/)

    const nqri = invoke('nqri', good, join(dir, 'native-parity-unverified.nqri'))
    assert.notEqual(nqri.status, 0, 'self-hosted NQRI must remain fail-closed without a distributed runtime')
    assert.match(nqri.stderr, /NQR-PGAS302/)

    const rejected = [
      '@partition(strategy: "scatter", nodes: 2) let dataset: [i64; 16]',
      '@partition(strategy: "block", nodes: 0) let dataset: [i64; 16]',
      '@partition(strategy: "block", nodes: 5000) let dataset: [i64; 16]',
      '@partition(strategy: "block", nodes: 2) let dataset: [u8; 16]',
      '@partition(strategy: "block", nodes: 2) let dataset: [i64; 0]',
      '@partition(strategy: "cyclic", nodes: 2) let dataset: i64',
      '@partition(strategy: "cyclic", nodes: 2) let dataset: [i64; 16] = []',
      '@bogus() let dataset: [i64; 16]',
      '@partition(strategy: "block", nodes: 2) let dataset: [i64; 4]\nlet local = dataset',
      '@partition(strategy: "block", nodes: 2) let dataset: [i64; 4]\nlet ptr = &dataset[0]',
      '@partition(strategy: "block", nodes: 2) let dataset: [i64; 4]\nprint(slice(dataset))',
      '@partition(strategy: "block", nodes: 2) let dataset: [i64; 4]\nfunction sneaky(): i64 { return dataset[0] }',
      'function sneaky(): int { @partition(strategy: "block", nodes: 2) let data: [i64; 4] return 0 }'
    ]
    for (let i = 0; i < rejected.length; ++i) {
      const file = join(dir, 'reject-' + i + '.noqeri')
      await writeFile(file, rejected[i] + '\n')
      const result = invoke('check', file)
      assert.notEqual(result.status, 0, 'unsafe partition spec was accepted: ' + rejected[i])
    }
    const cyclic = join(dir, 'cyclic.noqeri')
    await writeFile(cyclic, '@partition(strategy: "cyclic", nodes: 3) let values: [i64; 10]\n')
    const cycle = invoke('nir', cyclic)
    assert.equal(cycle.status, 0, cycle.stderr)
    assert.match(cycle.stdout, /@partition values strategy=cyclic nodes=3 elements=10/)
  } finally {
    await rm(dir, { recursive: true, force: true })
  }
})
