import {mkdtemp,writeFile,readFile,rm} from 'node:fs/promises'
import {tmpdir} from 'node:os'
import {join} from 'node:path'
import {createPackageManager,compareSemver,safeRelative,parseCoordinate} from '../Runtime/package-manager.mjs'

function require(ok,message){if(!ok)throw new Error(message)}
require(compareSemver('1.10.0','1.9.0')>0,'numeric semver ordering failed')
require(compareSemver('2.0.0-beta.1','2.0.0')<0,'prerelease ordering failed')
require(compareSemver('1.0.0-alpha.10','1.0.0-alpha.2')>0,'numeric prerelease ordering failed')
require(parseCoordinate('noqeri/supabase@1.0.0').version==='1.0.0','coordinate parsing failed')
for(const bad of ['../x','/x','a\\b','a//b']){let rejected=false;try{safeRelative(bad)}catch{rejected=true}require(rejected,`unsafe path accepted: ${bad}`)}

const root=await mkdtemp(join(tmpdir(),'noqeri-package-host-'))
try{
  await writeFile(join(root,'project.nqon'),'name = host-test\nversion = "1.0.0"\nentry = main.noqeri\npackages {\n  a = "noqeri/a@1.0.0"\n}\n')
  await writeFile(join(root,'main.noqeri'),'import package "noqeri/a"\n')
  const index={packages:[
    {namespace:'noqeri',name:'a',versions:[{version:'1.0.0',path:'packages/noqeri/a/1.0.0',yanked:false}]},
    {namespace:'noqeri',name:'b',versions:[{version:'1.0.0',path:'packages/noqeri/b/1.0.0',yanked:false}]}
  ]}
  const files={
    'https://registry.test/index.json':JSON.stringify(index),
    'mem://a-manifest':'package { name: "noqeri/a" version: "1.0.0" entry: "src/a.noqeri" }\ndependencies { b: "noqeri/b@1.0.0" }\n',
    'mem://a-src':'module a\nexport function a(): int { return 1 }\n',
    'mem://b-manifest':'package { name: "noqeri/b" version: "1.0.0" entry: "src/b.noqeri" }\n',
    'mem://b-src':'module b\nexport function b(): int { return 2 }\n'
  }
  const lists={
    'https://api.test/packages/noqeri/a/1.0.0?ref=main':[{type:'file',path:'packages/noqeri/a/1.0.0/package.noqeri',download_url:'mem://a-manifest'},{type:'dir',path:'packages/noqeri/a/1.0.0/src'}],
    'https://api.test/packages/noqeri/a/1.0.0/src?ref=main':[{type:'file',path:'packages/noqeri/a/1.0.0/src/a.noqeri',download_url:'mem://a-src'}],
    'https://api.test/packages/noqeri/b/1.0.0?ref=main':[{type:'file',path:'packages/noqeri/b/1.0.0/package.noqeri',download_url:'mem://b-manifest'},{type:'dir',path:'packages/noqeri/b/1.0.0/src'}],
    'https://api.test/packages/noqeri/b/1.0.0/src?ref=main':[{type:'file',path:'packages/noqeri/b/1.0.0/src/b.noqeri',download_url:'mem://b-src'}]
  }
  const encoder=new TextEncoder()
  const response=text=>{const bytes=encoder.encode(text);return{ok:true,status:200,headers:{get(){return String(bytes.length)}},async arrayBuffer(){return bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength)}}}
  const fetchImpl=async url=>{if(url in lists)return response(JSON.stringify(lists[url]));if(url in files)return response(files[url]);throw new Error('unexpected URL '+url)}
  const home=join(root,'.noqeri')
  const manager=createPackageManager({home,indexUrl:'https://registry.test/index.json',githubContents:'https://api.test/',fetchImpl})
  const previous=process.cwd();process.chdir(root)
  try{
    const installed=await manager.installProject()
    require(installed.length===2,'transitive dependency not installed')
    const lock=await readFile(join(root,'noqeri.lock'),'utf8')
    require(lock.includes('noqeri/a')&&lock.includes('noqeri/b'),'transitive lock entries missing')
    const resolved=await manager.resolvePackageImports(join(root,'main.noqeri'))
    require(resolved.length===1&&resolved[0].coordinate==='noqeri/a','source package resolution failed')
    const entry=resolved[0].entry
    await writeFile(entry,(await readFile(entry,'utf8'))+'// tamper\n')
    const offlineManager=createPackageManager({home,offline:true})
    let tamperRejected=false
    try{await offlineManager.fetchPackage('noqeri/a','1.0.0')}catch(error){tamperRejected=String(error.message).includes('integrity mismatch')||String(error.message).includes('counts mismatch')}
    require(tamperRejected,'tampered cache was accepted in offline mode')
  }finally{process.chdir(previous)}
  console.log('Noqeri package manager host tests passed')
}finally{await rm(root,{recursive:true,force:true})}
