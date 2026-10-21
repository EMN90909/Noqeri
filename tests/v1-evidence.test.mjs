import test from 'node:test'
import assert from 'node:assert/strict'
import {mkdtemp,mkdir,writeFile,readFile,rm} from 'node:fs/promises'
import {tmpdir} from 'node:os'
import {dirname,join} from 'node:path'
import {spawnSync} from 'node:child_process'
import {verifyV1Evidence} from '../Tools/release/verify-v1-evidence.mjs'
const rootUrl=new URL('../',import.meta.url)
test('release v1 evidence verifier has valid node syntax',()=>{
 const path=new URL('../Tools/release/verify-v1-evidence.mjs',import.meta.url).pathname
 const checked=spawnSync(process.execPath,['--check',path],{encoding:'utf8'})
 assert.equal(checked.status,0,checked.stderr)
})
test('production gate refuses missing platform/NoeSQL/private bootstrap evidence',async()=>{
 const root=await mkdtemp(join(tmpdir(),'noqeri-release-failclosed-'))
 try{
  await mkdir(join(root,'release'),{recursive:true})
  await writeFile(join(root,'release/platforms-v1.json'),await readFile(new URL('../release/platforms-v1.json',import.meta.url)))
  const out=await verifyV1Evidence({root})
  assert.equal(out.passed,false)
  assert.ok(out.errors.some(x=>x.includes('compiler qualification')))
  assert.ok(out.errors.some(x=>x.includes('signed')||x.includes('signature')))
  assert.ok(out.errors.some(x=>x.includes('Stage-1 independent trust')))
  assert.ok(out.errors.some(x=>x.includes('noesql-registry')))
  assert.ok(out.errors.some(x=>x.includes('unqualified release platform')))
 }finally{await rm(root,{recursive:true,force:true})}
})
test('fabricated all-green report without GPG proof and actual artifacts is rejected',async()=>{
 const root=await mkdtemp(join(tmpdir(),'noqeri-evidence-tamper-'))
 try{
  const paths=['release/platforms-v1.json','build/release-qualification/report.json','dist/release/release-manifest.json','dist/release/platform-test-evidence.json','dist/release/ecosystem-evidence.json','build/native-selfhost-proof.txt']
  for(const p of paths)await mkdir(dirname(join(root,p)),{recursive:true})
  await writeFile(join(root,paths[0]),await readFile(new URL('../release/platforms-v1.json',import.meta.url)))
  await writeFile(join(root,paths[1]),JSON.stringify({version:'1.0.0',passed:true,steps:[{passed:true}]}))
  await writeFile(join(root,paths[2]),JSON.stringify({version:'1.0.0',signature:'checksums.txt.asc',artifacts:[{path:'unbuilt-linux-x86_64',bytes:1,sha256:'0'.repeat(64)}]}))
  await writeFile(join(root,paths[3]),JSON.stringify({platforms:[]}))
  await writeFile(join(root,paths[4]),JSON.stringify({suites:[]}))
  await writeFile(join(root,paths[5]),'verified=true\nequivalent=true\nstage1_independently_trusted=false\n')
  const result=await verifyV1Evidence({root})
  assert.equal(result.passed,false)
  assert.ok(result.errors.some(s=>s.includes('artifact')))
  assert.ok(result.errors.some(s=>s.includes('Stage-1')))
  assert.ok(result.errors.some(s=>s.includes('unqualified ecosystem integration')))
 }finally{await rm(root,{recursive:true,force:true})}
})
