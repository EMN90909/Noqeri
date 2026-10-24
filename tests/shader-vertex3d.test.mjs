import test from 'node:test'
import assert from 'node:assert/strict'
import {mkdtemp,readFile,writeFile,rm} from 'node:fs/promises'
import {tmpdir} from 'node:os'
import {join} from 'node:path'
import {spawnSync} from 'node:child_process'

const compiler=process.env.NOQERI_COMPILER || '/tmp/noqeri-graphics-build/noqeri'
const invoke=(...args)=>spawnSync(compiler,args,{encoding:'utf8',timeout:20000})

test('Noqeri shader compiler emits vertex clip-position entry and independent fragment',async()=>{
  const dir=await mkdtemp(join(tmpdir(),'noqeri-vertex-'))
  try{
    for(const [source,stage,expected] of [
      ['vertex3d','vertex',['@vertex','@location(0) position: vec4<f32>','@builtin(position) vec4<f32>','noqeri_shade(position)']],
      ['fragment3d','fragment',['@fragment','@location(0) vec4<f32>']]
    ]){
      const out=join(dir,source+'.wgsl')
      const result=invoke('shader','examples/shaders/'+source+'.noqeri',out,'--stage='+stage)
      assert.equal(result.status,0,result.stderr)
      const wgsl=await readFile(out,'utf8')
      for(const marker of expected)assert.ok(wgsl.includes(marker),source+' '+marker)
    }
  }finally{await rm(dir,{recursive:true,force:true})}
})
test('vertex stage rejects unsupported signatures, host operations and resource access',async()=>{
  const dir=await mkdtemp(join(tmpdir(),'noqeri-vertex-reject-'))
  try{
    const invalid=[
      'export function shade(x: float, y: float): vec4 { return vec4(x, y, 0.1, 1.0) }',
      'export function shade(value: vec3): vec4 { return vec4(0.0) }',
      'export function shade(value: vec4): float { return value.x }',
      'export function shade(value: vec4): vec4 { return sampleTexture(value.xy) }',
      'export function shade(value: vec4): vec4 { return clockMillis() as vec4 }'
    ]
    for(let i=0;i<invalid.length;i++){
      const input=join(dir,i+'.noqeri'),out=join(dir,i+'.wgsl')
      await writeFile(input,invalid[i]+'\n')
      await writeFile(out,'keep-last-good')
      const result=invoke('shader',input,out,'--stage=vertex')
      assert.notEqual(result.status,0,'accepted invalid vertex shader '+i)
      assert.equal(await readFile(out,'utf8'),'keep-last-good')
    }
  }finally{await rm(dir,{recursive:true,force:true})}
})
