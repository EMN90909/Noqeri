import {test} from 'node:test'
import assert from 'node:assert/strict'
import {mkdtemp,writeFile,readFile,rm} from 'node:fs/promises'
import {join} from 'node:path'
import {tmpdir} from 'node:os'
import {createHash} from 'node:crypto'
import {gzipSync} from 'node:zlib'
import {createPackageManager} from '../Runtime/package-manager.mjs'
function pack(names){
 const chunks=[]
 const oct=(n,width)=>n.toString(8).padStart(width-1,'0')+'\0'
 for(const [name,bytes] of names){
  const header=Buffer.alloc(512)
  header.write(name,0,100);header.write(oct(0o644,8),100);header.write(oct(0,8),108);header.write(oct(0,8),116)
  header.write(oct(bytes.length,12),124);header.write(oct(0,12),136)
  header.fill(32,148,156);header[156]=48;header.write('ustar\0',257);header.write('00',263)
  let checksum=0;for(const ch of header)checksum+=ch;header.write(oct(checksum,8),148)
  chunks.push(header,bytes,Buffer.alloc((512-bytes.length%512)%512))
 }
 chunks.push(Buffer.alloc(1024))
 return gzipSync(Buffer.concat(chunks),{mtime:0})
}
test('CLI installs Registry /v1 archived dependencies, verifies integrity and refuses tampered offline cache',async()=>{
 const dir=await mkdtemp(join(tmpdir(),'noqeri-registry-v1-'))
 const previous=process.cwd()
 try {
  const files=[['package.noqeri',Buffer.from('package { name: "noqeri/demo" version: "0.4.0" entry: "src/main.noqeri" }\n')],
   ['src/main.noqeri',Buffer.from('module demo\nexport function answer(): int { return 42 }\n')]]
  const bytes=pack(files),hash='sha256:'+createHash('sha256').update(bytes).digest('hex')
  const release={version:'0.4.0',integrity:hash,path:'packages/noqeri/demo/0.4.0',yanked:true}
  const index={protocol:'v1',packages:[{namespace:'noqeri',name:'demo',versions:[release]}]}
  const endpoint='https://registry.test/v1/packages/noqeri/demo/0.4.0'
  const fetched=[]
  const fetchImpl=async url=>{
   fetched.push(String(url))
   if(url==='https://registry.test/v1/index')return new Response(JSON.stringify(index))
   if(url===endpoint)return new Response(JSON.stringify({namespace:'noqeri',name:'demo',...release}))
   if(url===endpoint+'/download')return new Response(bytes)
   throw Error('unexpected non-Registry URL: '+url)
  }
  await writeFile(join(dir,'project.noqeri'),'project { name: "registry-test" version: "0.1.0" entry: "main.noqeri" }\n')
  await writeFile(join(dir,'main.noqeri'),'module main\n')
  process.chdir(dir)
  const pm=createPackageManager({home:join(dir,'home'),registryUrl:'https://registry.test',fetchImpl})
  await assert.rejects(pm.fetchPackage('noqeri/demo'),/no usable release/)
  const installed=await pm.fetchPackage('noqeri/demo','0.4.0')
  assert.equal(installed.version,'0.4.0')
  assert.equal(installed.archiveIntegrity,hash)
  assert.ok(fetched.every(url=>url.startsWith('https://registry.test/v1/')))
  const fetchedBefore=fetched.length
  await pm.addPackage('noqeri/demo@0.4.0')
  assert.equal(fetched.length,fetchedBefore+1) // index queried; cached package reused
  const lock=await readFile(join(dir,'noqeri.lock'),'utf8')
  assert.match(lock,/noqeri\/demo 0\.4\.0/)
  const actual=join(installed.path,'src','main.noqeri')
  await writeFile(actual,(await readFile(actual,'utf8'))+'//tampered')
  const offline=createPackageManager({home:join(dir,'home'),offline:true})
  await assert.rejects(offline.fetchPackage('noqeri/demo','0.4.0'),/integrity mismatch/)
 }finally{process.chdir(previous);await rm(dir,{force:true,recursive:true})}
})
test('Registry client rejects archive integrity mismatch before filesystem writes',async()=>{
 const dir=await mkdtemp(join(tmpdir(),'noqeri-bad-registry-'))
 try {
  const data=pack([['package.noqeri',Buffer.from('package { entry: "main.noqeri" }')],['main.noqeri',Buffer.from('module main')]])
  const index={protocol:'v1',packages:[{namespace:'noqeri',name:'bad',versions:[{version:'1.0.0',yanked:false,integrity:'sha256:'+'0'.repeat(64),path:'packages/noqeri/bad/1.0.0'}]}]}
  const fetchImpl=async url=>new Response(String(url).endsWith('/download')?data:JSON.stringify(String(url).endsWith('/v1/index')?index:{namespace:'noqeri',name:'bad',version:'1.0.0',yanked:false,integrity:'sha256:'+'0'.repeat(64)}))
  const pm=createPackageManager({home:dir,registryUrl:'https://registry.test',fetchImpl})
  await assert.rejects(pm.fetchPackage('noqeri/bad','1.0.0'),/integrity mismatch/)
 }finally{await rm(dir,{force:true,recursive:true})}
})
