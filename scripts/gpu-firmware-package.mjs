#!/usr/bin/env node
// Noqeri GPU firmware *development* artifact, not a vendor firmware format.
// Deliberately no hardware device access, flashing, signing, or drivers.
import {readFile,writeFile} from 'node:fs/promises'
import {createHash} from 'node:crypto'
import {extname} from 'node:path'

const maxPayload=1024*1024
const sha256=bytes=>createHash('sha256').update(bytes).digest('hex')
function invariant(ok,msg){if(!ok)throw new Error(msg)}
async function loadSmall(path,limit){
  const bytes=await readFile(path)
  invariant(bytes.length>0 && bytes.length<=limit,'Input exceeds bounded firmware development artifact size')
  return bytes
}
async function verify(file){
  const raw=await loadSmall(file,2*maxPayload)
  const envelope=JSON.parse(raw.toString('utf8'))
  invariant(envelope && typeof envelope==='object' &&
    Object.keys(envelope).sort().join(',') ===
    'format,payloadBase64,payloadSha256,sourceSha256,target',
    'Unrecognized firmware development envelope schema')
  invariant(envelope.format==='noqeri-gpu-firmware-dev-v1' &&
    envelope.target==='sim-x86_64',
    'Only the simulated x86_64 GPU-control profile is supported')
  invariant(typeof envelope.sourceSha256==='string' &&
    /^[0-9a-f]{64}$/.test(envelope.sourceSha256) &&
    typeof envelope.payloadSha256==='string' &&
    /^[0-9a-f]{64}$/.test(envelope.payloadSha256),
    'Invalid source or payload digest')
  invariant(typeof envelope.payloadBase64==='string' &&
    /^[A-Za-z0-9+/]+={0,2}$/.test(envelope.payloadBase64) &&
    envelope.payloadBase64.length <= Math.ceil(maxPayload/3)*4,
    'Invalid payload encoding or length')
  const payload=Buffer.from(envelope.payloadBase64,'base64')
  invariant(payload.length>0 && payload.length<=maxPayload &&
    payload.toString('base64')===envelope.payloadBase64 &&
    sha256(payload)===envelope.payloadSha256,
    'Firmware simulation payload checksum mismatch')
  return {target:envelope.target,bytes:payload.length,sha256:envelope.payloadSha256}
}
async function main(){
  const [action,...args]=process.argv.slice(2)
  if(action==='pack'){
    invariant(args.length===3,'pack requires <source.noqeri> <compiled-sim.bin> <output.nqfw.json>')
    const [sourcePath,payloadPath,output]=args
    invariant(extname(sourcePath)==='.noqeri' && extname(payloadPath)==='.bin' &&
      output.endsWith('.nqfw.json'),'Expected source .noqeri, simulator .bin and .nqfw.json artifact')
    const source=await loadSmall(sourcePath,maxPayload)
    const payload=await loadSmall(payloadPath,maxPayload)
    const result={
      format:'noqeri-gpu-firmware-dev-v1',target:'sim-x86_64',
      sourceSha256:sha256(source),payloadSha256:sha256(payload),
      payloadBase64:payload.toString('base64')
    }
    // No in-place overwrite of an existing artifact: callers must
    // consciously choose a new path for immutable experiment evidence.
    await writeFile(output,JSON.stringify(result,null,2)+'\n',{flag:'wx',mode:0o600})
    console.log(JSON.stringify(await verify(output)))
  }else if(action==='verify'){
    invariant(args.length===1 && args[0].endsWith('.nqfw.json'),'verify requires <file.nqfw.json>')
    console.log(JSON.stringify(await verify(args[0])))
  }else{
    throw new Error('Usage: pack <source.noqeri> <simulator.bin> <output.nqfw.json> | verify <artifact.nqfw.json>. No flash operation is provided.')
  }
}
main().catch(error=>{console.error('NQR-GPU-FW: '+(error.message||String(error)));process.exitCode=1})
