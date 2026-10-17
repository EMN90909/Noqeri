#!/usr/bin/env node
// Local-only web conformance runner. Set NOQERI_BIN to a compiler executable.
import { spawnSync } from 'node:child_process'
import { mkdtempSync, rmSync, readFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { resolve, join, dirname } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'

const root=resolve(dirname(fileURLToPath(import.meta.url)),'..')
const bin=resolve(process.env.NOQERI_BIN||join(root,'build/noqeri'))
const temp=mkdtempSync(join(tmpdir(),'noqeri-web-smoke-'))
const results=[]
function invoke(args){
  const p=spawnSync(bin,args,{cwd:root,encoding:'utf8',timeout:15000,maxBuffer:2*1024*1024})
  return {ok:!p.error&&p.status===0,status:p.status,error:p.error?.message||'',stderr:p.stderr||'',stdout:p.stdout||''}
}
function record(name,pass,details=''){
  results.push({name,status:pass?'PASS':'FAIL',details})
  console.log((pass?'PASS ':'FAIL ')+name+' '+details)
}
function check(name,file,expected){
  const p=invoke(['check',file])
  record(name,p.ok===expected,'exit='+p.status+' '+(p.error||p.stderr.trim().slice(0,100)))
}
async function compile(name,file,verify){
  const output=join(temp,name+'.mjs')
  const p=invoke(['web',file,output])
  if(!p.ok){record(name,false,'web compiler unavailable or failed: '+(p.error||p.stderr.trim().slice(0,180)));return}
  try{
    const source=readFileSync(output,'utf8')
    if(/\beval\s*\(|new\s+Function\s*\(/.test(source))throw Error('dynamic JS code detected')
    const mod=await import(pathToFileURL(output).href)
    record(name,Boolean(await verify(mod)),'compiled web module')
  }catch(error){record(name,false,error.message)}
}
try{
  const v=invoke(['--version'])
  record('CLI launch',v.ok,v.stdout.trim()||v.error)
  if(!v.ok)throw Error('No working compiler binary; set NOQERI_BIN')
  check('web exports source','tests/web_smoke/01_exports.nqr',true)
  check('JSON source','tests/web_smoke/02_json.nqr',true)
  check('DOM host source','tests/web_smoke/03_dom.nqr',true)
  check('arithmetic source','tests/web_smoke/04_arithmetic.nqr',true)
  check('invalid type rejected','tests/web_smoke/05_invalid_type.nqr',false)
  await compile('exports','tests/web_smoke/01_exports.nqr',m=>m.apiStatus?.()==='ok'&&m.greeting?.('Ada')==='hello Ada')
  await compile('json','tests/web_smoke/02_json.nqr',m=>JSON.parse(m.response?.()||'null')?.status==='ok')
  await compile('arithmetic','tests/web_smoke/04_arithmetic.nqr',m=>m.add?.(8,13)===21&&m.multiply?.(6,7)===42)
  const prior=globalThis.NoqeriHost
  try{
    globalThis.NoqeriHost={domText:(selector,value)=>selector==='#app'&&value==='Hello from Noqeri'}
    await compile('dom','tests/web_smoke/03_dom.nqr',m=>m.render?.()===true)
  }finally{
    if(prior===undefined)delete globalThis.NoqeriHost
    else globalThis.NoqeriHost=prior
  }
  const invalid=invoke(['web','tests/web_smoke/06_invalid_browser_asm.nqr',join(temp,'invalid.mjs')])
  record('reject native assembly in browser',!invalid.ok,'exit='+invalid.status)
}catch(error){record('initialization',false,error.message)}
finally{
  rmSync(temp,{recursive:true,force:true})
  const passed=results.filter(r=>r.status==='PASS').length
  console.log(JSON.stringify({compiler:bin,passed,failed:results.length-passed,results},null,2))
  if(passed!==results.length)process.exitCode=1
}
