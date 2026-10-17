#!/usr/bin/env node
// Noqeri local release qualification; no GitHub Actions, no silent skipped checks.
import {spawnSync} from 'node:child_process'
import {existsSync,mkdirSync,writeFileSync} from 'node:fs'
import {resolve} from 'node:path'
const root=resolve(new URL('..',import.meta.url).pathname)
const reportDir=resolve(root,'build/release-qualification')
mkdirSync(reportDir,{recursive:true})
const steps=[]
function run(name,command,args,cwd=root,env={}) {
 const start=Date.now()
 const r=spawnSync(command,args,{cwd,encoding:'utf8',timeout:900000,maxBuffer:8*1024*1024,env:{...process.env,...env}})
 const entry={name,command:[command,...args],status:r.status,signal:r.signal,elapsed_ms:Date.now()-start,passed:r.status===0&&!r.error&&!r.signal,error:r.error?.message||null,stdout:(r.stdout||'').slice(-30000),stderr:(r.stderr||'').slice(-30000)}
 steps.push(entry);console.log((entry.passed?'PASS ':'FAIL ')+name+(entry.error?' '+entry.error:''))
 return entry.passed
}
// Standalone Node tests are valuable even without a compiler seed.\nrun('web-component-regressions','node',['--test','tests/web-compiler.test.mjs','tests/web-project.test.mjs'])\nconst seed=process.env.NOQERI_STAGE0
if(!seed) {
 console.error('NOQERI_STAGE0 is mandatory for independent full compiler bootstrap')
 steps.push({name:'trusted-bootstrap-seed',passed:false,error:'NOQERI_STAGE0 is not configured'})
} else {
 const built=run('build-selfhost', 'sh',['scripts/build.sh'],root,{NOQERI_STAGE0:seed})
 if(built) {
  run('stage2-abi','node',['scripts/verify-stage2-module.mjs'])
  run('core-conformance','sh',['scripts/test_core.sh'],root,{NOQERI_RUNTIME_TESTS:'1'})
  run('web-compiler','node',['--test','tests/web-compiler.test.mjs','tests/web-project.test.mjs'])
  run('web-smoke','node',['scripts/test_web_dev.mjs'])
  run('full-historical-suite','sh',['scripts/test.sh'])
  if(existsSync(resolve(root,'Compiler/selfhost/bootstrap-local.json')))
   run('bootstrap-proof','node',['scripts/bootstrap-stage2.mjs'])
  else steps.push({name:'bootstrap-proof',passed:false,error:'bootstrap-local.json missing'})
 } else {
  for(const name of ['stage2-abi','core-conformance','web-smoke','full-historical-suite','bootstrap-proof'])
   steps.push({name,passed:false,error:'blocked by bootstrap build'})
 }
}
const siblings=[['registry-index','../noqeri-registry','scripts/verify-index.mjs'],['website-source-installer','../noqeri-web','scripts/source-install.test.mjs']]
for(const [name,dir,file] of siblings) {
 const cwd=resolve(root,dir)
 if(existsSync(resolve(cwd,file)))run(name,'node',[file],cwd)
 else steps.push({name,passed:false,error:'repository or test missing: '+cwd+'/'+file})
}
const report={schema:1,generated_at:new Date().toISOString(),qualification:'local-build',passed:steps.length>0&&steps.every(s=>s.passed),steps}
writeFileSync(resolve(reportDir,'report.json'),JSON.stringify(report,null,2)+'\n')
console.log('Report: '+resolve(reportDir,'report.json'))
process.exit(report.passed?0:1)
