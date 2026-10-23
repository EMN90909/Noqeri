import test from 'node:test'
import assert from 'node:assert/strict'
import {mkdtemp,writeFile,rm} from 'node:fs/promises'
import {tmpdir} from 'node:os'
import {join} from 'node:path'
import {spawnSync} from 'node:child_process'

const compiler=process.env.NOQERI_PGAS_COMPILER || '/tmp/noqeri-pgas-build/noqeri'

test('native compiler retains partition attributes, validates types, and fails closed on unsupported execution',async()=>{
  const dir=await mkdtemp(join(tmpdir(),'pgas-attr-'))
  try{
    const good=join(dir,'typed.noqeri')
    await writeFile(good,'@partition(strategy: "block", nodes: 2) let dataset: [i64; 16]\n')
    const invoke=(command,file=good,...args)=>spawnSync(compiler,[command,file,...args],{encoding:'utf8',timeout:12000})
    const check=invoke('check')
    assert.equal(check.status,0,check.stderr)
    const nir=invoke('nir')
    assert.equal(nir.status,0,nir.stderr)
    assert.match(nir.stdout,/@partition dataset strategy=block nodes=2 elements=16/)
    const run=invoke('run')
    assert.notEqual(run.status,0,'interpreter must not treat partitioned data as local storage')
    assert.match(run.stderr,/PGAS partitions require a distributed backend/)
    const build=invoke('build',good,join(dir,'silent-local.s'))
    assert.notEqual(build.status,0,'native backend must not silently compile remote array to local memory')
    assert.match(build.stderr,/NQR-PGAS301/)
    const nqri=invoke('nqri',good,join(dir,'silent-local.nqri'))
    assert.notEqual(nqri.status,0)
    assert.match(nqri.stderr,/NQR-PGAS302/)
    const rejected=[
      '@partition(strategy: "scatter", nodes: 2) let dataset: [i64; 16]',
      '@partition(strategy: "block", nodes: 0) let dataset: [i64; 16]',
      '@partition(strategy: "block", nodes: 5000) let dataset: [i64; 16]',
      '@partition(strategy: "block", nodes: 2) let dataset: [u8; 16]',
      '@partition(strategy: "block", nodes: 2) let dataset: [i64; 0]',
      '@partition(strategy: "cyclic", nodes: 2) let dataset: i64',
      '@partition(strategy: "cyclic", nodes: 2) let dataset: [i64; 16] = []',
      '@bogus() let dataset: [i64; 16]'
    ]
    for(let i=0;i<rejected.length;++i){
      const file=join(dir,'reject-'+i+'.noqeri')
      await writeFile(file,rejected[i]+'\n')
      const result=invoke('check',file)
      assert.notEqual(result.status,0,'invalid partition spec accepted: '+rejected[i])
    }
    const cyclic=join(dir,'cyclic.noqeri')
    await writeFile(cyclic,'@partition(strategy: "cyclic", nodes: 3) let values: [i64; 10]\n')
    const cycle=invoke('nir',cyclic)
    assert.equal(cycle.status,0,cycle.stderr)
    assert.match(cycle.stdout,/@partition values strategy=cyclic nodes=3 elements=10/)
  }finally{await rm(dir,{recursive:true,force:true})}
})
