// Registry v1 HTTPS client and bounded deterministic .nqpkg reader.
// No GitHub/raw-content fallback, no unverified archive installation.
import { createHash } from 'node:crypto'
import { gunzipSync } from 'node:zlib'
const MAX_ARCHIVE=8*1024*1024, MAX_EXPANDED=9*1024*1024, MAX_FILES=256
export const IDENT=/^[a-z0-9][a-z0-9-]*$/
export const VERSION=/^(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$/
export const INTEGRITY=/^sha256:[0-9a-f]{64}$/
export function apiBase(value=process.env.NOQERI_REGISTRY_API_URL){
  if(!value)throw new Error('NOQERI_REGISTRY_API_URL must name a Registry v1 server')
  const url=new URL(value)
  if(url.username||url.password||url.search||url.hash||(url.protocol!=='https:'&&!(url.protocol==='http:'&&['localhost','127.0.0.1','[::1]'].includes(url.hostname))))throw new Error('Registry requires HTTPS or localhost development')
  return url.href.replace(/\/$/,'')
}
export function safeArchivePath(name){
  if(typeof name!=='string'||!name||name.includes('\\')||name.includes('\0')||name.startsWith('/')||name.split('/').some(x=>!x||x==='.'||x==='..'))throw new Error('unsafe package archive path')
  return name
}
export function decodeArchive(bytes,expectedIntegrity){
  if(!INTEGRITY.test(expectedIntegrity))throw new Error('missing archive SHA-256 integrity')
  if(bytes.length>MAX_ARCHIVE)throw new Error('archive exceeds size limit')
  if('sha256:'+createHash('sha256').update(bytes).digest('hex')!==expectedIntegrity)throw new Error('archive integrity mismatch')
  const input=gunzipSync(bytes,{maxOutputLength:MAX_EXPANDED})
  if(input.length%512!==0)throw new Error('invalid tar block alignment')
  const files=[],names=new Set();let total=0,position=0,ended=false
  while(position<input.length){
    const block=input.subarray(position,position+512);position+=512
    if(block.every(b=>b===0)){ended=true;break}
    const magic=block.toString('ascii',257,263)
    if(magic!=='ustar\0')throw new Error('unsupported archive format')
    const type=block[156];if(type!==0 && type!==48)throw new Error('archive contains non-file entry')
    if(block.subarray(345,500).some(b=>b!==0))throw new Error('tar prefix paths are unsupported')
    const name=safeArchivePath(block.toString('utf8',0,100).replace(/\0.*$/s,''))
    if(Buffer.byteLength(name)>100||names.has(name))throw new Error('duplicate or long archive path')
    names.add(name)
    const stored=block.toString('ascii',148,156).replace(/\0.*$/s,'').trim()
    if(!/^[0-7]+$/.test(stored))throw new Error('invalid tar checksum')
    const copy=Buffer.from(block);copy.fill(32,148,156)
    if(copy.reduce((n,v)=>n+v,0)!==parseInt(stored,8))throw new Error('tar header checksum mismatch')
    const rawSize=block.toString('ascii',124,136).replace(/\0.*$/s,'').trim()
    if(!/^[0-7]+$/.test(rawSize))throw new Error('invalid tar size')
    const size=parseInt(rawSize,8)
    if(size>MAX_ARCHIVE-total||files.length>=MAX_FILES)throw new Error('archive size/file limit exceeded')
    total+=size; if(total>MAX_ARCHIVE)throw new Error('archive unpacked size exceeds limit')
    const padded=Math.ceil(size/512)*512
    if(position+padded>input.length)throw new Error('truncated archive')
    files.push({name,bytes:Uint8Array.from(input.subarray(position,position+size))})
    position+=padded
  }
  if(!ended||input.subarray(position).some(b=>b!==0))throw new Error('invalid archive terminator')
  const manifest=files.find(f=>f.name==='package.noqeri')||files.find(f=>f.name==='package.nqr')
  if(!manifest)throw new Error('package manifest missing')
  const content=new TextDecoder('utf-8',{fatal:true}).decode(manifest.bytes)
  const entry=content.match(/\bentry\s*:\s*"([^"]+)"/)?.[1]
  safeArchivePath(entry)
  if(!files.some(f=>f.name===entry))throw new Error('package entry not found')
  return {files,entry,manifestName:manifest.name,legacy:manifest.name==='package.nqr'}
}
export function sha256Archive(bytes){return 'sha256:'+createHash('sha256').update(bytes).digest('hex')}
