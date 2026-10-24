#!/usr/bin/env node
// This gate requires an independently produced Stage-2 GPU emitter.
// Never satisfy it by falling back to the Stage-0 C++ compiler.
import {readFile, mkdtemp, rm, realpath} from 'node:fs/promises'
import {createHash} from 'node:crypto'
import {spawnSync} from 'node:child_process'
import {tmpdir} from 'node:os'
import {join} from 'node:path'

const stage0=process.env.NOQERI_STAGE0_SHADER_COMPILER
const stage2=process.env.NOQERI_STAGE2_SHADER_COMPILER
const proofPath=process.env.NOQERI_STAGE2_SHADER_PROOF
const fail=reason=>{throw new Error('NQR-GPU-STAGE2-NOT-QUALIFIED: '+reason)}
const hash=bytes=>createHash('sha256').update(bytes).digest('hex')
const fixtures=[
  ['examples/shaders/color_gradient.noqeri','fragment'],
  ['examples/shaders/grayscale_fragment.noqeri','fragment'],
  ['examples/shaders/invert_compute.noqeri','compute'],
  ['examples/shaders/vector_fragment.noqeri','fragment'],
  ['examples/shaders/texture_fragment.noqeri','fragment'],
  ['examples/shaders/vector_compute.noqeri','compute']
]
async function main(){
  if(!stage0||!stage2||!proofPath)
    fail('provide Stage-0 and independently self-hosted Stage-2 compiler paths and an auditable proof file')
  if(await realpath(stage0)===await realpath(stage2))
    fail('Stage-2 shader compiler resolves to the Stage-0 executable')
  const bytes=await readFile(stage2)
  const proof=JSON.parse(await readFile(proofPath,'utf8'))
  if(proof.stage2_shader_compiler_sha256!==hash(bytes)||
      proof.shader_codegen_selfhosted!==true||
      proof.stage1_seed_verified!==true||
      proof.stage2_stage3_equal!==true||
      proof.delegates_to_stage0_shader_backend!==false)
    fail('provenance must attest an independently built self-hosted shader code generator')
  const dir=await mkdtemp(join(tmpdir(),'noqeri-shader-parity-'))
  try {
    for(let i=0;i<fixtures.length;i++){
      const [source,stage]=fixtures[i]
      const outputs=[]
      for(const [compiler,tag] of [[stage0,'stage0'],[stage2,'stage2']]){
        const output=join(dir,i+'-'+tag+'.wgsl')
        const result=spawnSync(compiler,['shader',source,output,'--stage='+stage],
          {encoding:'utf8',timeout:30000})
        if(result.error||result.status!==0)
          fail(tag+' failed to compile '+source+': '+(result.error?.message||result.stderr))
        outputs.push(await readFile(output))
      }
      if(!outputs[0].equals(outputs[1]))
        fail('Stage-0 and Stage-2 WGSL differ for '+source)
      process.stdout.write('PASS byte-equivalent WGSL: '+source+'\n')
    }
    console.log('GPU_SHADER_STAGE2_PARITY=PASS; verify independently that proof origin is trusted')
  }finally {await rm(dir,{recursive:true,force:true})}
}
main().catch(error=>{console.error(String(error.message||error));process.exitCode=1})
