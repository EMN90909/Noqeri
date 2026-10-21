import {mkdtemp,writeFile,readFile,mkdir,rm} from 'node:fs/promises'
import {tmpdir} from 'node:os'
import {join} from 'node:path'
import {createHash} from 'node:crypto'
import {gzipSync} from 'node:zlib'
import {createPackageManager,compareSemver,safeRelative,parseCoordinate} from '../Runtime/package-manager-v1.mjs'

function require(ok,message){if(!ok)throw new Error(message)}
const digest=data=>'sha256:'+createHash('sha256').update(data).digest('hex')
function header(name,size){
 const out=Buffer.alloc(512)
 const oct=(n,w)=>n.toString(8).padStart(w-1,'0')+'\0'
 out.write(name,0,100);out.write(oct(0o644,8),100);out.write(oct(0,8),108)
 out.write(oct(0,8),116);out.write(oct(size,12),124);out.write(oct(0,12),136)
 out.fill(32,148,156);out[156]=48;out.write('ustar\0',257);out.write('00',263)
 let total=0;for(const x of out)total+=x
 out.write(oct(total,8),148)
 return out
}
function makePackage(files){
 const output=[]
 for(const [name,source] of Object.entries(files).sort(([a],[b])=>a<b?-1:a>b?1:0)){
  const bytes=Buffer.from(source)
  output.push(header(name,bytes.length),bytes)
  if(bytes.length%512)output.push(Buffer.alloc(512-bytes.length%512))
 }
 output.push(Buffer.alloc(1024))
 return gzipSync(Buffer.concat(output),{mtime:0,level:9})
}
require(compareSemver('1.10.0','1.9.0')>0,'numeric semver sorting failed')
require(compareSemver('2.0.0-beta.1','2.0.0')<0,'prerelease sorting failed')
require(parseCoordinate('noqeri/a@1.0.0').version==='1.0.0','coordinate parsing failed')
for(const bad of ['../x','/x','a\\b','a//b']) {let rejected=false;try{safeRelative(bad)}catch{rejected=true}require(rejected,'unsafe path accepted: '+bad)}

const root=await mkdtemp(join(tmpdir(),'noqeri-registry-cli-'))
try{
 await writeFile(join(root,'project.nqon'),'name = host-test\nversion = "1.0.0"\nentry = main.noqeri\npackages {\n a = "noqeri/a@1.0.0"\n}\n')
 await writeFile(join(root,'main.noqeri'),'import package "noqeri/a"\n')
 const bytes={
  a:makePackage({'package.noqeri':'package { name: "noqeri/a" version: "1.0.0" entry: "src/a.noqeri" }\ndependencies {\n b: "noqeri/b@1.0.0"\n}\n','src/a.noqeri':'module a\nexport function a(): int { return 1 }\n'}),
  b:makePackage({'package.noqeri':'package { name: "noqeri/b" version: "1.0.0" entry: "src/b.noqeri" }\n','src/b.noqeri':'module b\nexport function b(): int { return 2 }\n'})
 }
 const archiveIntegrity={a:digest(bytes.a),b:digest(bytes.b)}
 const index={packages:['a','b'].map(name=>({namespace:'noqeri',name,versions:[{version:'1.0.0',yanked:false,archiveIntegrity:archiveIntegrity[name],entry:'src/'+name+'.noqeri'},{version:'2.0.0',yanked:true}]}))}
 const seen=[]
 const fetchImpl=async url=>{
  const path=new URL(url).pathname;seen.push(path)
  if(path==='/v1/index')return new Response(JSON.stringify(index))
  for(const name of ['a','b']){
   const base='/v1/packages/noqeri/'+name+'/1.0.0'
   if(path===base)return new Response(JSON.stringify({version:'1.0.0',yanked:false,archiveIntegrity:archiveIntegrity[name],entry:'src/'+name+'.noqeri'}))
   if(path===base+'/download')return new Response(bytes[name])
  }
  throw new Error('unexpected upstream '+url)
 }
 const home=join(root,'.noqeri')
 const manager=createPackageManager({home,apiBase:'https://registry.test',fetchImpl})
 const cwd=process.cwd();process.chdir(root)
 try{
  const installed=await manager.installProject()
  require(installed.length===2,'transitive Registry API dependency missing')
  require(seen.every(x=>x.startsWith('/v1/')),'non-v1 request occurred')
  const lock=await readFile(join(root,'noqeri.lock'),'utf8')
  require(lock.includes('noqeri/a')&&lock.includes('noqeri/b'),'lock dependencies missing')
  const imports=await manager.resolvePackageImports(join(root,'main.noqeri'))
  require(imports.length===1&&imports[0].coordinate==='noqeri/a','import resolution failed')
  await writeFile(imports[0].entry,(await readFile(imports[0].entry,'utf8'))+'// corrupt\n')
  const offline=createPackageManager({home,offline:true})
  let refused=false;try{await offline.fetchPackage('noqeri/a','1.0.0')}catch(e){refused=/integrity|counts/.test(e.message)}
  require(refused,'tampered package cache accepted')
  refused=false;try{await manager.fetchPackage('noqeri/a','2.0.0')}catch(e){refused=/no usable release/.test(e.message)}
  require(refused,'yanked pinned release accepted')
  const tampered=createPackageManager({home:join(root,'tamper-home'),apiBase:'https://registry.test',fetchImpl:async url=>String(url).endsWith('/download')?new Response(Buffer.from('corrupted')):fetchImpl(url)})
  refused=false;try{await tampered.fetchPackage('noqeri/b','1.0.0')}catch(e){refused=/integrity mismatch/.test(e.message)||/archive/.test(e.message)}
  require(refused,'corrupt downloaded artifact accepted')
 }finally{process.chdir(cwd)}
 console.log('Registry API package manager host tests passed')
}finally{await rm(root,{recursive:true,force:true})}
