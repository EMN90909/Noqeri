#!/usr/bin/env node
import {readFile} from 'node:fs/promises'
import {fileURLToPath} from 'node:url'
import {resolve,join} from 'node:path'
const sourceRoot=fileURLToPath(new URL('../..',import.meta.url))
const get=(root,path)=>readFile(join(root,path),'utf8')
export async function verifyV1Contract(root=sourceRoot){
 const errors=[], inspect=async(path,rx,label)=>{
   try{if(!rx.test(await get(root,path)))errors.push(label+' inconsistent: '+path)}
   catch(e){errors.push(label+' unavailable: '+e.message)}
 }
 await inspect('VERSION',/^1\.0\.0\s*$/,'VERSION')
 await inspect('CMakeLists.txt',/project\(noqeri VERSION 1\.0\.0 LANGUAGES CXX\)/,'CMake version')
 await inspect('Include/noqeri/version.hpp',/NOQERI_LANGUAGE_VERSION\s*=\s*"1\.0"/,'language version')
 await inspect('Include/noqeri/version.hpp',/NOQERI_COMPILER_VERSION\s*=\s*"1\.0\.0"/,'compiler version')
 await inspect('project.nqon',/^version\s*=\s*"1\.0\.0"\s*$/m,'project version')
 for(const [member,expected] of [['Major',1],['Minor',0],['Patch',0]]){
  await inspect('Compiler/selfhost/driver.noqeri',new RegExp('nqCompilerVersion'+member+'\\(\\): int \\{ return '+expected+' \\}'),'selfhost '+member)
 }
 let platforms
 try{platforms=JSON.parse(await get(root,'release/platforms-v1.json'))}catch(e){errors.push('platform policy missing/invalid: '+e.message)}
 if(platforms){
  if(platforms.version!=='1.0.0'||platforms.schema!==1||!Array.isArray(platforms.platforms))errors.push('invalid platform policy version or schema')
  else {
   const ids=new Set();let required=0
   for(const p of platforms.platforms){
    if(typeof p.id!=='string'||ids.has(p.id))errors.push('duplicate or malformed platform id')
    ids.add(p.id)
    if(p.release_required){required++;if(p.tier!=='candidate')errors.push('release target must be candidate until qualification: '+p.id)}
    if(p.tier==='supported')errors.push('unsupported unqualified platform claim: '+p.id)
   }
   if(required<2 || !ids.has('linux-x86_64') || !ids.has('windows-x86_64'))errors.push('required Linux/Windows x86-64 platform records missing')
  }
 }
 return {version:'1.0.0',passed:errors.length===0,errors,platforms:platforms?.platforms?.length??0}
}
if(process.argv[1]&&resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const result=await verifyV1Contract()
 console.log(JSON.stringify(result,null,2))
 if(!result.passed)process.exitCode=1
}
