import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtemp,writeFile,readFile,rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { spawnSync } from 'node:child_process'

const compiler=process.env.NOQERI_PGAS_COMPILER || '/tmp/noqeri-pgas-build/noqeri'
test('pure Noqeri remote closure compiler rejects host operations and unsafe captures',async()=>{
  const dir=await mkdtemp(join(tmpdir(),'pgas-closure-'))
  try{
    const rejected=[
      'let secret: i64 = 5 as i64\nexport function remote(x: i64): i64 { return x + secret }',
      'export function remote(x: i64): i64 { return clockMillis() }',
      'export function remote(x: i64): i64 { print(x) return x }',
      'export function remote(x: i64): i64 { return x + 300 as i64 }',
      'export function remote(x: i64): i64 { let y: i64 = 4 as i64 return x + y }',
      'export function remote(x: i64): i64 { return x }\nexport function remote2(x: i64): i64 { return x }',
      'export function remote(x: i64): i64 { return x + 1 as i64 }\nprint(1)'
    ]
    for(let i=0;i<rejected.length;++i){
      const path=join(dir,'invalid'+i+'.noqeri')
      await writeFile(path,rejected[i]+'\n')
      const output=join(dir,'out.nqpg')
      const run=spawnSync(compiler,['pgas-compile',path,output],{encoding:'utf8',timeout:12000})
      assert.notEqual(run.status,0,'remote closure accepted unsafe program: '+rejected[i])
    }
    const valid=join(dir,'valid.noqeri')
    await writeFile(valid,'const captured: i64 = 5 as i64\nexport function remote(x: i64): i64 { return x + captured }\n')
    const output=join(dir,'valid.nqpg')
    const run=spawnSync(compiler,['pgas-compile',valid,output],{encoding:'utf8',timeout:12000})
    assert.equal(run.status,0,run.stderr)
    const bytes=await readFile(output)
    assert.deepEqual([...bytes],[1,2,5,3,14])
  }finally{await rm(dir,{recursive:true,force:true})}
})
