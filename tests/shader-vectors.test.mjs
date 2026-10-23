import test from 'node:test'
import assert from 'node:assert/strict'
import {mkdtemp,readFile,writeFile,rm} from 'node:fs/promises'
import {tmpdir} from 'node:os'
import {join} from 'node:path'
import {spawnSync} from 'node:child_process'

const compiler=process.env.NOQERI_COMPILER||'/tmp/noqeri-graphics-build/noqeri'
const compile=(source,output,stage)=>spawnSync(
  compiler,['shader',source,output,'--stage='+stage],
  {encoding:'utf8',timeout:12000}
)

test('Noqeri compiler emits vector compute, texture fragments and typed vector math',async()=>{
  const dir=await mkdtemp(join(tmpdir(),'noqeri-gpu-vectors-'))
  try{
    const valid=[
      ['vector_compute.noqeri','compute',[
        'array<vec4<f32>>', 'vec4<f32>(1.0f, 0.5f, 0.25f, 1.0f)',
        'fn noqeri_shade(nq_arg0: vec4<f32>) -> vec4<f32>'
      ]],
      ['textured_fragment.noqeri','fragment',[
        '@group(0) @binding(0) var nq_texture: texture_2d<f32>;',
        '@group(0) @binding(1) var nq_sampler: sampler;',
        'textureSample(nq_texture, nq_sampler,',
        'return noqeri_shade(position.x, position.y);'
      ]],
      ['vector_math_fragment.noqeri','fragment',[
        'distance(', 'normalize(', 'vec3<f32>(',
        'return noqeri_shade(position.x, position.y);'
      ]]
    ]
    for(const [name,stage,needles] of valid){
      const file='examples/shaders/'+name
      const destination=join(dir,name+'.wgsl')
      const r=compile(file,destination,stage)
      assert.equal(r.status,0,r.stderr)
      const wgsl=await readFile(destination,'utf8')
      for(const needle of needles)
        assert.ok(wgsl.includes(needle),name+' missing generated WGSL '+needle)
    }
  }finally{await rm(dir,{recursive:true,force:true})}
})
test('Shader vectors and textures reject bad dimension, stage, call and type combinations',async()=>{
  const dir=await mkdtemp(join(tmpdir(),'noqeri-shader-negative-'))
  try{
    const invalid=[
      ['vectorDimension','compute','export function shade(v: vec4): vec4 { return vec4(0.0, 0.0, 0.0) }'],
      ['vectorScalar','compute','export function shade(v: vec4): vec4 { return v + 1.0 }'],
      ['badSwizzle','fragment','export function shade(x: float, y: float): vec4 { const v: vec2 = vec2(x,y) return vec4(v.z,0.0,0.0,1.0) }'],
      ['textureCompute','compute','export function shade(x: vec4): vec4 { return sampleTexture(vec2(0.0,0.0)) }'],
      ['textureSignature','fragment','export function shade(x: u32, y: u32): vec4 { return sampleTexture(vec2(0.0,0.0)) }'],
      ['unsafeCall','fragment','export function shade(x: float, y: float): vec4 { return host("secret") }'],
      ['vectorResult','fragment','export function shade(x: float, y: float): vec3 { return vec3(1.0,1.0,1.0) }'],
      ['pointer','fragment','export function shade(x: float, y: float): vec4 { const p: *float = &x return vec4(1.0,1.0,1.0,1.0) }'],
      ['unknownTexture','fragment','export function shade(x: float, y: float): vec4 { return textureSample(vec2(x,y)) }'],
      ['invalidImport','fragment','import "../another.noqeri"\nexport function shade(x: float,y: float): vec4 { return vec4(1.0,1.0,1.0,1.0) }']
    ]
    for(const [name,stage,source] of invalid){
      const path=join(dir,name+'.noqeri'),out=join(dir,name+'.wgsl')
      await writeFile(path,source+'\n')
      await writeFile(out,'last-known-good-WGSL')
      const r=compile(path,out,stage)
      assert.notEqual(r.status,0,'unsafe shader unexpectedly accepted '+name)
      assert.equal(await readFile(out,'utf8'),'last-known-good-WGSL')
    }
  }finally{await rm(dir,{recursive:true,force:true})}
})
