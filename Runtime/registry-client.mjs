import {createHash} from 'node:crypto'
import {gunzipSync} from 'node:zlib'
// The Registry v1 wire artifact is a gzip compressed ustar file, never GitHub
// source listings. Verify the raw archive digest before parsing any members.
const MAX_ARCHIVE=10*1024*1024,MAX_EXPANDED=16*1024*1024,MAX_FILES=256,MAX_MEMBER=8*1024*1024
const safe=name=>typeof name==='string'&&name.length>0&&!name.startsWith('/')&&!name.includes('\\')&&!name.includes('\0')&&!/^[A-Za-z]:/.test(name)&&name.split('/').every(p=>p&&p!=='.'&&p!=='..')
function numberField(buf){const str=buf.toString('ascii').replace(/\0.*$/s,'').trim();if(!/^[0-7]+$/.test(str))throw Error('invalid tar numeric field');return parseInt(str,8)}
export function decodePackageArchive(bytes,expectedIntegrity) {
 const input=Buffer.from(bytes)
 if(input.length>MAX_ARCHIVE)throw Error('registry archive size limit exceeded')
 if(!/^sha256:[0-9a-f]{64}$/.test(expectedIntegrity||''))throw Error('registry archive has no SHA-256 integrity metadata')
 const digest='sha256:'+createHash('sha256').update(input).digest('hex')
 if(digest!==expectedIntegrity)throw Error('registry archive integrity mismatch')
 const raw=gunzipSync(input,{maxOutputLength:MAX_EXPANDED})
 if(raw.length%512!==0)throw Error('invalid tar padding')
 const files=[],names=new Set();let offset=0,done=false,bytesTotal=0
 while(offset<raw.length){
  const h=raw.subarray(offset,offset+512);offset+=512
  if(h.every(b=>b===0)){done=true;break}
  if(h.toString('ascii',257,262)!=='ustar')throw Error('unsupported archive format')
  const declared=numberField(h.subarray(148,156))
  let sum=0;for(let i=0;i<512;i++)sum+=(i>=148&&i<156)?32:h[i]
  if(sum!==declared)throw Error('tar header checksum mismatch')
  const name=h.subarray(0,100).toString('utf8').split('\0')[0]
  const size=numberField(h.subarray(124,136))
  if(!safe(name)||names.has(name))throw Error('unsafe or duplicate archive path')
  if(h[156]!==48&&h[156]!==0)throw Error('archive member is not a regular file')
  if(size>MAX_MEMBER||size>MAX_EXPANDED-bytesTotal||offset+size>raw.length||++files.length>MAX_FILES)throw Error('archive file limit exceeded')
  const content=Buffer.from(raw.subarray(offset,offset+size))
  files.push({name,bytes:content});names.add(name);bytesTotal+=size
  offset+=Math.ceil(size/512)*512
 }
 if(!done||raw.subarray(offset).some(b=>b!==0))throw Error('archive end marker missing or trailing garbage')
 if(!names.has('package.noqeri')&&!names.has('package.nqr'))throw Error('package manifest missing')
 return files
}
export function assertRegistryUrl(input){
 let u;try{u=new URL(input)}catch{throw Error('invalid NOQERI_REGISTRY_URL')}
 if(u.username||u.password||u.search||u.hash||u.pathname!=='/'||!['https:','http:'].includes(u.protocol))throw Error('registry base URL must be an origin')
 if(u.protocol!=='https:'&&!['localhost','127.0.0.1','[::1]'].includes(u.hostname))throw Error('remote registry requires HTTPS')
 return u.origin
}
