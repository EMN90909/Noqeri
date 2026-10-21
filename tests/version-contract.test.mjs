import test from 'node:test'
import assert from 'node:assert/strict'
import {mkdtemp,readFile,writeFile,mkdir,rm} from 'node:fs/promises'
import {tmpdir} from 'node:os'
import {join,dirname} from 'node:path'
import {verifyV1Contract} from '../Tools/release/verify-v1-contract.mjs'
const files=['VERSION','CMakeLists.txt','Include/noqeri/version.hpp','project.nqon','Compiler/selfhost/driver.noqeri','release/platforms-v1.json']
test('v1.0.0 compiler, project and self-hosted version contract agree',async()=>{
 const result=await verifyV1Contract()
 assert.equal(result.passed,true,JSON.stringify(result.errors))
 assert.equal(result.version,'1.0.0')
})
test('contract rejects semver drift and fabricated supported platform tier',async()=>{
 const root=await mkdtemp(join(tmpdir(),'noqeri-version-contract-'))
 try{
  for(const p of files){await mkdir(dirname(join(root,p)),{recursive:true});await writeFile(join(root,p),await readFile(new URL('../'+p,import.meta.url)))}
  await writeFile(join(root,'VERSION'),'0.7.0\n')
  let result=await verifyV1Contract(root)
  assert.equal(result.passed,false)
  assert.match(result.errors.join(' '),/VERSION/)
  await writeFile(join(root,'VERSION'),'1.0.0\n')
  const manifest=JSON.parse(await readFile(join(root,'release/platforms-v1.json')))
  manifest.platforms[0].tier='supported'
  await writeFile(join(root,'release/platforms-v1.json'),JSON.stringify(manifest))
  result=await verifyV1Contract(root)
  assert.equal(result.passed,false)
  assert.match(result.errors.join(' '),/unqualified/)
 }finally{await rm(root,{recursive:true,force:true})}
})
