import test from 'node:test'
import assert from 'node:assert/strict'
import {mkdtemp,writeFile,readFile,rm} from 'node:fs/promises'
import {tmpdir} from 'node:os'
import {join} from 'node:path'
import {spawnSync} from 'node:child_process'

const compiler=process.env.NOQERI_COMPILER || '/tmp/noqeri-graphics-build/noqeri'
const run=(...args)=>spawnSync(compiler,args,{encoding:'utf8',timeout:20000})

test('typed Noqeri vector and 2D sampled texture shaders compile to guarded WGSL',async()=>{
  const dir=await mkdtemp(join(tmpdir(),'noqeri-texture-'))
  try {
    const cases=[
      ['vector_gradient_fragment.noqeri','fragment',['vec2<f32>','vec3<f32>','vec4<f32>','nq_local0']],
      ['texture_fragment.noqeri','fragment',['texture_2d<f32>','var nq_sampler: sampler','textureSample(nq_texture, nq_sampler','textureDimensions(nq_texture']],
      ['texture_load_fragment.noqeri','fragment',['textureLoad(nq_texture, vec2<i32>','@group(0) @binding(0)','vec4<f32>']],
      ['vector_compute.noqeri','compute',['@compute @workgroup_size(128)','input_pixels: array<vec4<f32>>','output_pixels: array<vec4<f32>>','mix(']]
    ]
    for(const [file,stage,wanted] of cases){
      const output=join(dir,file+'.wgsl')
      const args=['shader','examples/shaders/'+file,output,'--stage='+stage]
      if(stage==='compute')args.push('--workgroup-size=128')
      const done=run(...args)
      assert.equal(done.status,0,done.stderr)
      const wgsl=await readFile(output,'utf8')
      for(const needle of wanted)assert.ok(wgsl.includes(needle),file+': '+needle)
      if(file==='texture_load_fragment.noqeri')assert.ok(!wgsl.includes('var nq_sampler: sampler'))
      if(file==='vector_gradient_fragment.noqeri')assert.ok(!wgsl.includes('var nq_texture: texture_2d'))
    }
  } finally {await rm(dir,{recursive:true,force:true})}
})
test('shader vector and texture intrinsic declarations fail closed on invalid ABI',async()=>{
  const dir=await mkdtemp(join(tmpdir(),'noqeri-texture-neg-'))
  try {
    const invalid=[
      ['unknown-external',[
        'extern function launchShell(): u32',
        'export function shade(x: u32, y: u32): u32 { return launchShell() }'
      ].join('\n')],
      ['fake-vector',[
        'record NqVec2 { x: float y: u32 }',
        'extern function nqVec2(x: float, y: float): NqVec2',
        'export function shade(x: float, y: float): float { return x }'
      ].join('\n')],
      ['wrong-builtin-signature',[
        'record NqVec2 { x: float y: float }',
        'extern function nqVec2(x: bool, y: float): NqVec2',
        'export function shade(x: float, y: float): float { return x }'
      ].join('\n')],
      ['texture-in-compute',[
        'record NqVec2 { x: float y: float }',
        'record NqVec4 { x: float y: float z: float w: float }',
        'extern function nqTextureSample(uv: NqVec2): NqVec4',
        'extern function nqVec2(x: float, y: float): NqVec2',
        'export function shade(v: NqVec4): NqVec4 { return nqTextureSample(nqVec2(0.0, 0.0)) }'
      ].join('\n')],
      ['native-escape',[
        'record NqVec4 { x: float y: float z: float w: float }',
        'extern function nqTextureSample(uv: NqVec4): NqVec4',
        'export function shade(x: float, y: float): NqVec4 { return nqTextureSample(null) }'
      ].join('\n')]
    ]
    for(const [label,source] of invalid){
      const file=join(dir,label+'.noqeri'),out=join(dir,label+'.wgsl')
      await writeFile(file,source+'\n')
      await writeFile(out,'last valid shader')
      const done=run('shader',file,out,'--stage='+(
        label==='texture-in-compute'?'compute':'fragment'))
      assert.notEqual(done.status,0,'incorrectly accepted '+label)
      assert.equal(await readFile(out,'utf8'),'last valid shader','corrupted valid output: '+label)
    }
  }finally {await rm(dir,{recursive:true,force:true})}
})
