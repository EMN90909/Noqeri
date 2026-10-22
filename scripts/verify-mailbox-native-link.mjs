#!/usr/bin/env node
// Executable acceptance test, not a source-level scan. Fail closed.
import { spawnSync } from 'node:child_process'
import { mkdtempSync, existsSync, readFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
const root=resolve(import.meta.dirname,'..')
const compiler=resolve(process.argv[2]||'build/noqeri')
const cxx=process.env.CXX||'c++'
const lib=resolve(process.argv[3]||'build/libnoqeri_mailbox_runtime.a')
function run(exe,args) {
  const p=spawnSync(exe,args,{cwd:root,encoding:'utf8',timeout:60000})
  if(p.error||p.status!==0){
    throw Error(`${exe} ${args.join(' ')} failed: ${p.error?.message||p.stderr||p.stdout||p.status}`)
  }
  return p.stdout
}
if(!existsSync(compiler)||!existsSync(lib))throw Error('Compiler and mailbox archive required. Build both first.')
const dir=mkdtempSync(join(tmpdir(),'noqeri-native-link-'))
try{
  const asm=join(dir,'link.s'),bin=join(dir,'link')
  run(compiler,['build',resolve(root,'tests/native_mailbox_link.noqeri'),asm,'-O0'])
  run(cxx,[asm,resolve(root,'Runtime/native/aot_runner.cpp'),lib,'-pthread','-o',bin])
  const output=run(bin,[])
  if(!output.includes('NATIVE_MAILBOX_LINK_OK'))throw Error('Native mailbox fixture failed: '+output)
  console.log('Native Noqeri -> C ABI -> host socket integration PASS')
} finally {rmSync(dir,{recursive:true,force:true})}
