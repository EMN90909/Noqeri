import test from 'node:test'
import assert from 'node:assert/strict'
import {mkdtemp,readFile,writeFile,rm} from 'node:fs/promises'
import {tmpdir} from 'node:os'
import {join} from 'node:path'
import {spawnSync} from 'node:child_process'

const compiler=process.env.NOQERI_COMPILER || '/tmp/noqeri-graphics-build/noqeri'
const run=(...args)=>spawnSync(compiler,args,{encoding:'utf8',timeout:15000})

test('Noqeri shader command generates actual compute and fragment WGSL',async()=>{
  const dir=await mkdtemp(join(tmpdir(),'noqeri-shader-'))
  try{
    const cases=[
      ['examples/shaders/color_gradient.noqeri','fragment',[
        '@fragment','@builtin(position)','@location(0)','fn noqeri_shade(nq_arg0: u32, nq_arg1: u32)',
        'let rgb = vec3<f32>(f32((p >> 16u) & 255u)','return vec4<f32>(rgb * (1.0 / 255.0), 1.0);'
      ]],
      ['examples/shaders/invert_compute.noqeri','compute',[
        '@compute @workgroup_size(64)','@group(0) @binding(0)','@group(0) @binding(1)',
        'arrayLength(&input_pixels)','fn noqeri_shade(nq_arg0: u32)'
      ]]
    ]
    for(const [source,stage,expected] of cases){
      const dest=join(dir,stage+'.wgsl')
      const out=run('shader',source,dest,'--stage='+stage)
      assert.equal(out.status,0,out.stderr)
      const wgsl=await readFile(dest,'utf8')
      for(const fragment of expected)assert.ok(wgsl.includes(fragment),fragment)
      assert.ok(wgsl.startsWith('// Generated from typechecked Noqeri source'))
    }
  }finally{await rm(dir,{recursive:true,force:true})}
})

test('Noqeri shader compiler refuses host calls, mutable state and unsupported stages',async()=>{
  const dir=await mkdtemp(join(tmpdir(),'noqeri-shader-reject-'))
  try{
    const invalid=[
      ['badcall.noqeri','export function shade(value: u32): u32 { return clockMillis() as u32 }'],
      ['sideeffect.noqeri','export function shade(value: u32): u32 { print(value) return value }'],
      ['mutable.noqeri','let secret: u32 = 2 as u32\nexport function shade(value: u32): u32 { return value + secret }'],
      ['badliteral.noqeri','export function shade(value: u32): u32 { return value + 4294967296 as u32 }'],
      ['wrongparams.noqeri','export function shade(x: u32, y: u32): u32 { return x }']
    ]
    for(const [file,source] of invalid){
      const p=join(dir,file),output=join(dir,file+'.wgsl')
      await writeFile(p,source+'\n')
      const result=run('shader',p,output,'--stage=compute')
      assert.notEqual(result.status,0,'accepted unsupported shader source: '+file)
    }
    const invalidStage=run('shader','examples/shaders/color_gradient.noqeri',
      join(dir,'oops.wgsl'),'--stage=geometry')
    assert.notEqual(invalidStage.status,0)
    assert.match(invalidStage.stderr,/NQR-GPU102/)
  }finally{await rm(dir,{recursive:true,force:true})}
})

test('shader compiler folds exact bounded u32 arithmetic and rejects folded zero division',async()=>{
  const dir=await mkdtemp(join(tmpdir(),'noqeri-shader-peephole-'))
  try {
    const valid=join(dir,'fold.noqeri'),dest=join(dir,'fold.wgsl')
    await writeFile(valid,`export function shade(value: u32): u32 {
      const factor: u32 = ((2 as u32) + (3 as u32)) * (4 as u32)
      return value + factor
    }\n`)
    const compiled=run('shader',valid,dest,'--stage=compute')
    assert.equal(compiled.status,0,compiled.stderr)
    const generated=await readFile(dest,'utf8')
    assert.match(generated,/let nq_local0: u32 = 20u;/)
    const invalid=join(dir,'invalid.noqeri'),keep=join(dir,'keep.wgsl')
    await writeFile(invalid,`export function shade(value: u32): u32 {
      return value / ((3 as u32) - (3 as u32))
    }\n`)
    await writeFile(keep,'preserve-previous-verified-result')
    const rejected=run('shader',invalid,keep,'--stage=compute')
    assert.notEqual(rejected.status,0,'folded zero divisor must fail closed')
    assert.equal(await readFile(keep,'utf8'),'preserve-previous-verified-result')
  } finally {await rm(dir,{recursive:true,force:true})}
})
