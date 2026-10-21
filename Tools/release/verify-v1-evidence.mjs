#!/usr/bin/env node
// Strict release gate. Validates real artifact bytes, signed checksums, compiler
// bootstrap provenance and OS-specific test records. Does not fabricate proof.
import {readFile,lstat} from 'node:fs/promises'
import {spawnSync} from 'node:child_process'
import {resolve,join} from 'node:path'
import {createHash} from 'node:crypto'
const SHA=/^[a-f0-9]{64}$/,REV=/^[a-f0-9]{40}$/
const digest=b=>createHash('sha256').update(b).digest('hex')
const readJson=async(path,errors,label)=>{try{return JSON.parse(await readFile(path,'utf8'))}catch(e){errors.push(label+': '+e.message);return null}}
export async function verifyV1Evidence({root=process.cwd(),qualification='build/release-qualification/report.json',artifacts='dist/release',bootstrap='build/native-selfhost-proof.txt'}={}){
 root=resolve(root);const errors=[]
 const manifest=await readJson(resolve(root,artifacts,'release-manifest.json'),errors,'release manifest')
 const qual=await readJson(resolve(root,qualification),errors,'compiler qualification')
 const platformPolicy=await readJson(join(root,'release/platforms-v1.json'),errors,'platform support policy')
 const platformRuns=await readJson(resolve(root,artifacts,'platform-test-evidence.json'),errors,'platform test provenance')
 const ecosystem=await readJson(resolve(root,artifacts,'ecosystem-evidence.json'),errors,'private ecosystem qualification')
 const rev=spawnSync('git',['rev-parse','HEAD'],{cwd:root,encoding:'utf8'})
 const revision=rev.status===0&&REV.test(rev.stdout.trim())?rev.stdout.trim():null
 if(!revision)errors.push('source revision is not a Git commit')
 if(!qual||qual.version!=='1.0.0'||qual.passed!==true||!Array.isArray(qual.steps)||qual.steps.length===0||qual.steps.some(s=>s.passed!==true)||qual.source_revision!==revision) errors.push('Noqeri compiler qualification did not pass on the exact source revision')
 if(!manifest||manifest.version!=='1.0.0'||manifest.source_revision!==revision||!Array.isArray(manifest.artifacts)||manifest.artifacts.length===0) errors.push('artifact manifest missing, empty or on a different source revision')
 if(manifest?.signature!=='checksums.txt.asc')errors.push('unsigned artifact manifest')
 const artifactRoot=resolve(root,artifacts)
 const hashes=new Map()
 if(Array.isArray(manifest?.artifacts)){
  for(const item of manifest.artifacts){
   const path=item?.path
   if(typeof path!=='string'||!path||path.startsWith('/')||path.includes('\\')||path.split('/').some(z=>!z||z==='.'||z==='..')){errors.push('unsafe artifact path');continue}
   try{
    const full=resolve(artifactRoot,path)
    if(!full.startsWith(artifactRoot+'/'))throw Error('escaped artifacts directory')
    const st=await lstat(full)
    if(!st.isFile()||st.nlink!==1)throw Error('not a regular isolated file')
    const bytes=await readFile(full),hash=digest(bytes)
    if(!SHA.test(item.sha256)||hash!==item.sha256||bytes.length!==item.bytes)errors.push('artifact hash/size mismatch: '+path)
    if(hashes.has(path))errors.push('duplicate release artifact: '+path)
    hashes.set(path,hash)
   }catch(e){errors.push('artifact '+path+': '+e.message)}
  }
 }
 let checksums=''
 try{checksums=await readFile(join(artifactRoot,'checksums.txt'),'utf8')}catch(e){errors.push('checksums.txt missing: '+e.message)}
 for(const [path,hash]of hashes)if(!checksums.split('\n').includes(hash+'  '+path))errors.push('missing checksum entry: '+path)
 const sig=join(artifactRoot,'checksums.txt.asc')
 try{await lstat(sig);const gpg=spawnSync('gpg',['--batch','--verify',sig,join(artifactRoot,'checksums.txt')],{encoding:'utf8',timeout:12000});if(gpg.status!==0)errors.push('GPG checksum signature invalid or untrusted')}catch(e){errors.push('GPG signature unavailable: '+e.message)}
 let proof
 try{
  const lines=(await readFile(resolve(root,bootstrap),'utf8')).split(/\r?\n/).filter(Boolean)
  proof=Object.fromEntries(lines.map(line=>{const i=line.indexOf('=');if(i<1)throw Error('malformed proof');return[line.slice(0,i),line.slice(i+1)]}))
 }catch(e){errors.push('native self-host proof unavailable: '+e.message)}
 if(!proof||proof.verified!=='true'||proof.equivalent!=='true'||proof.stage1_independently_trusted!=='true'||!SHA.test(proof.seed_sha256||'')||!SHA.test(proof.stage2_sha256||'')||proof.stage2_sha256!==proof.stage3_sha256)
  errors.push('Stage-1 independent trust and Stage-2/Stage-3 equivalence not proven')
 const required=platformPolicy?.platforms?.filter(p=>p.release_required) || []
 if(platformPolicy?.version!=='1.0.0'||required.length<2||required.some(p=>p.tier!=='candidate'))errors.push('invalid supported-platform candidate policy')
 for(const platform of required){
  const evidence=platformRuns?.platforms?.find(x=>x.id===platform.id)
  if(!evidence||evidence.result!=='passed'||evidence.source_revision!==revision||!/^https:\/\/github\.com\/EMN90909\/Noqeri\/actions\/runs\/\d+$/.test(evidence.workflow_url||'')||hashes.get(evidence.artifact)!==evidence.sha256||!SHA.test(evidence.sha256||''))
   errors.push('unqualified release platform: '+platform.id)
 }
 for(const subsystem of ['noesql-registry','registry-cli','registry-web']){
  const evidence=ecosystem?.suites?.find(s=>s.name===subsystem)
  if(!evidence||evidence.result!=='passed'||evidence.noqeri_revision!==revision||!REV.test(evidence.integration_revision||'')||!/^https:\/\/github\.com\//.test(evidence.workflow_url||''))errors.push('unqualified ecosystem integration: '+subsystem)
 }
 return {schema:1,version:'1.0.0',passed:errors.length===0,source_revision:revision,errors}
}
if(import.meta.url==='file://'+process.argv[1]){
 const [qualification,artifacts]=process.argv.slice(2)
 const result=await verifyV1Evidence({qualification,artifacts})
 console.log(JSON.stringify(result,null,2))
 if(!result.passed)process.exitCode=1
}
