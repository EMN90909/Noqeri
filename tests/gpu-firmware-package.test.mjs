import test from 'node:test'
import assert from 'node:assert/strict'
import {mkdtemp,readFile,writeFile,rm} from 'node:fs/promises'
import {join} from 'node:path'
import {tmpdir} from 'node:os'
import {spawnSync} from 'node:child_process'

const run=(...args)=>spawnSync(process.execPath,['scripts/gpu-firmware-package.mjs',...args],
  {encoding:'utf8',timeout:5000})
test('Firmware development package has stable checksums, rejects tampering and never flashes',async()=>{
  const root=await mkdtemp(join(tmpdir(),'nq-fw-'))
  try{
    const src=join(root,'sim.noqeri'),bin=join(root,'sim.bin'),out=join(root,'sim.nqfw.json')
    await writeFile(src,'module gpu_sim\n')
    await writeFile(bin,Buffer.from([1,2,3,4,5,6]))
    let result=run('pack',src,bin,out)
    assert.equal(result.status,0,result.stderr)
    const original=await readFile(out,'utf8')
    assert.deepEqual(JSON.parse(result.stdout),{
      target:'sim-x86_64',bytes:6,sha256:JSON.parse(original).payloadSha256
    })
    result=run('verify',out)
    assert.equal(result.status,0,result.stderr)
    assert.notEqual(run('pack',src,bin,out).status,0,'must not overwrite existing artifact')
    const modified=JSON.parse(original)
    modified.payloadBase64=Buffer.from([7,8,9]).toString('base64')
    await writeFile(out,JSON.stringify(modified))
    result=run('verify',out)
    assert.notEqual(result.status,0)
    assert.match(result.stderr,/checksum mismatch/)
    assert.notEqual(run('flash',out).status,0)
    assert.notEqual(run('pack',out,bin,join(root,'invalid.nqfw.json')).status,0)
  }finally{await rm(root,{recursive:true,force:true})}
})
