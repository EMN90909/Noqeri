import test from 'node:test'
import assert from 'node:assert/strict'
import {mkdtemp,readFile,writeFile,rm} from 'node:fs/promises'
import {tmpdir} from 'node:os'
import {join} from 'node:path'
import {spawnSync} from 'node:child_process'

const compiler=process.env.NOQERI_COMPILER || '/tmp/noqeri-graphics-build/noqeri'
const invoke=(...args)=>spawnSync(compiler,args,{encoding:'utf8',timeout:20000})

test('GPU profile v3 emits typed vectors, swizzles and texture declarations',async()=>{
  const dir=await mkdtemp(join(tmpdir(),'noqeri-shader-v3-'))
  try {
    for(const [name,stage,wanted] of [
      ['vector_fragment','fragment',['vec2<f32>','vec3<f32>','vec4<f32>','(nq_local0).x','@fragment']],
      ['texture_fragment','fragment',['texture_2d<f32>','nq_sampler: sampler','textureDimensions(nq_texture)','textureSampleLevel(nq_texture, nq_sampler','@fragment']],
      ['vector_compute','compute',['array<vec4<f32>>','(nq_arg0).xyz','@compute @workgroup_size(64)']]
    ]) {
      const output=join(dir,name+'.wgsl')
      const result=invoke('shader','examples/shaders/'+name+'.noqeri',output,'--stage='+stage)
      assert.equal(result.status,0,result.stderr)
      const wgsl=await readFile(output,'utf8')
      for(const text of wanted)assert.ok(wgsl.includes(text),name+': missing '+text)
    }
  } finally {await rm(dir,{recursive:true,force:true})}
})
test('GPU profile v3 rejects invalid swizzles, unauthorized calls and texture sampling in compute',async()=>{
  const dir=await mkdtemp(join(tmpdir(),'noqeri-shader-invalid-'))
  try {
    const sources=[
      'export function shade(x: float, y: float): vec4 { return vec4(vec2(x, y).z, 0.0, 0.0, 1.0) }',
      'export function shade(x: float, y: float): vec4 { return vec4(vec2(x, y).xy, 0.0, 0.0, 1.0) }',
      'export function shade(x: float, y: float): vec4 { return vec4(clockMillis(), y, 0.0, 1.0) }',
      'export function shade(x: float, y: float): vec4 { return sampleTexture(vec2(x, y)) + vec3(1.0) }',
      'export function shade(value: vec4): vec4 { return sampleTexture(value.xy) }',
      'export function shade(value: vec4): vec4 { return vec4(value.x, value.y, value.z, value.x / 0.0) }'
    ]
    for(let i=0;i<sources.length;i++){
      const file=join(dir,i+'.noqeri'),output=join(dir,i+'.wgsl')
      await writeFile(file,sources[i]+'\n')
      await writeFile(output,'retain-last-good')
      const stage=i>=4?'compute':'fragment'
      const result=invoke('shader',file,output,'--stage='+stage)
      assert.notEqual(result.status,0,'accepted invalid shader '+i)
      assert.equal(await readFile(output,'utf8'),'retain-last-good')
    }
  } finally {await rm(dir,{recursive:true,force:true})}
})
