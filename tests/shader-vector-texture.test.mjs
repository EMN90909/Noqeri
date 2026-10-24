import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtemp, readFile, writeFile, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { spawnSync } from 'node:child_process'

const compiler=process.env.NOQERI_COMPILER || '/tmp/noqeri-graphics-build/noqeri'
const compile=(...args)=>spawnSync(compiler,args,{encoding:'utf8',timeout:18000})
const setup=async callback=>{
  const dir=await mkdtemp(join(tmpdir(),'noqeri-vector-shader-'))
  try {await callback(dir)} finally {await rm(dir,{recursive:true,force:true})}
}
test('GPU vec2/vec3/vec4, components, math and textures compile from Noqeri source',async()=>{
 await setup(async dir=>{
  const samples=[
   ['examples/shaders/vector_mix_compute.noqeri','compute',[
     'array<vec4<f32>>','dot(', 'mix(', '@workgroup_size(64)'
   ]],
   ['examples/shaders/vector_fragment.noqeri','fragment',[
     'fn noqeri_shade(nq_arg0: f32, nq_arg1: f32) -> vec4<f32>',
     'vec3<f32>(', 'normalize(', '.x)', 'return clamp(noqeri_shade('
   ]],
   ['examples/shaders/texture_fragment.noqeri','fragment',[
     'textureSample(nq_texture, nq_sampler,',
     '@group(0) @binding(0) var nq_texture: texture_2d<f32>;',
     '@group(0) @binding(1) var nq_sampler: sampler;',
     'return clamp(noqeri_shade('
   ]]
  ]
  for(const [source,stage,required] of samples){
    const file=join(dir,source.split('/').at(-1)+'.wgsl')
    const result=compile('shader',source,file,'--stage='+stage)
    assert.equal(result.status,0,result.stderr)
    const wgsl=await readFile(file,'utf8')
    for(const piece of required)assert.ok(wgsl.includes(piece),source+': '+piece)
  }
 })
})
test('vector and texture shader type errors fail closed without touching outputs',async()=>{
 await setup(async dir=>{
  const sources=[
   ['badSampleCompute','export function shade(value: Vec4): Vec4 { return textureSample(vec2(0.5,0.5)) }','compute'],
   ['sampleBadUV','export function shade(x: float,y: float): Vec4 { return textureSample(vec4(0.0,0.0,0.0,0.0)) }','fragment'],
   ['badVectorLength','export function shade(x: float,y: float): Vec4 { return vec4(x,y,0.5) }','fragment'],
   ['badComponent','export function shade(x: float,y: float): Vec4 { return vec4(x,y,0.0,1.0).x }','fragment'],
   ['badVectorType','export function shade(x: float,y: float): Vec4 { return vec4(x,y,0.0,1.0) + vec3(x,y,0.0) }','fragment'],
   ['badVectorBool','export function shade(value: Vec4): Vec4 { if value == value {return value} return value }','compute'],
   ['badTextureType','export function shade(x: float,y: float): u32 { return textureSample(vec2(x,y)) }','fragment'],
   ['badOpaqueCall','export function shade(x: float,y: float): Vec4 { return gpuHostColor(x,y) }','fragment']
  ]
  for(const [name,source,stage] of sources){
    const input=join(dir,name+'.noqeri'),output=join(dir,name+'.wgsl')
    await writeFile(input,source+'\n')
    await writeFile(output,'known-good')
    const result=compile('shader',input,output,'--stage='+stage)
    assert.notEqual(result.status,0,name+' was erroneously accepted')
    assert.equal(await readFile(output,'utf8'),'known-good')
  }
  const wrong='examples/shaders/texture_level_compute.noqeri'
  const result=compile('shader',wrong,join(dir,'wrong.wgsl'),'--stage=compute')
  assert.notEqual(result.status,0)
 })
})
