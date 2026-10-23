import test from 'node:test'
import assert from 'node:assert/strict'
import {mkdtemp, readFile, writeFile, rm} from 'node:fs/promises'
import {tmpdir} from 'node:os'
import {join} from 'node:path'
import {spawnSync} from 'node:child_process'

const compiler = process.env.NOQERI_COMPILER || '/tmp/noqeri-graphics-build/noqeri'
const invoke = (...args) => spawnSync(compiler, args, {encoding: 'utf8', timeout: 20000})
async function inTemp(task) {
  const dir=await mkdtemp(join(tmpdir(),'noqeri-shader-v2-'))
  try {await task(dir)} finally {await rm(dir,{recursive:true,force:true})}
}
test('shader v2 supports typed conditionals, locals and float GPU programs',async()=>{
  await inTemp(async dir=>{
    const specs=[
      ['examples/shaders/posterize_fragment.noqeri','fragment',[
        'fn noqeri_shade(nq_arg0: u32, nq_arg1: u32)','let nq_local0: u32',
        'if (','@fragment','16711680u','255u'
      ]],
      ['examples/shaders/float_compute.noqeri','compute',[
        'fn noqeri_shade(nq_arg0: f32) -> f32',
        'input_pixels: array<f32>','output_pixels: array<f32>',
        'let nq_local0: f32 = 0.5f;','@compute @workgroup_size(64)'
      ]],
      ['examples/shaders/grayscale_fragment.noqeri','fragment',[
        'fn noqeri_shade(nq_arg0: f32, nq_arg1: f32) -> f32',
        'clamp(noqeri_shade(position.x, position.y), 0.0, 1.0)',
        'vec4<f32>(grayscale, grayscale, grayscale, 1.0)'
      ]]
    ]
    for(const [source,stage,fragments] of specs){
      const output=join(dir,source.split('/').pop()+'.wgsl')
      const done=invoke('shader',source,output,'--stage='+stage)
      assert.equal(done.status,0,done.stderr)
      const wgsl=await readFile(output,'utf8')
      for(const part of fragments)assert.ok(wgsl.includes(part),source+': missing '+part)
    }
  })
})
test('shader workgroup sizes and CLI flags are strictly validated',async()=>{
  await inTemp(async dir=>{
    const src='examples/shaders/invert_compute.noqeri'
    for(const group of [32,64,128,256]){
      const output=join(dir,group+'.wgsl')
      const result=invoke('shader',src,output,'--workgroup-size='+group,'--stage=compute')
      assert.equal(result.status,0,result.stderr)
      assert.ok((await readFile(output,'utf8')).includes('@compute @workgroup_size('+group+')'))
    }
    const rejected=[
      ['--workgroup-size=0','--stage=compute'],['--workgroup-size=512','--stage=compute'],
      ['--workgroup-size=64','--workgroup-size=128','--stage=compute'],
      ['--stage=compute','--stage=compute'],['--stage=compute','--unknown'],
      ['--stage=compute','--unsafe'],['--stage=fragment','--workgroup-size=32'],
      ['--stage=geometry'],['--stage=compute','more-than-one.wgsl']
    ]
    for(const opts of rejected){
      const result=invoke('shader',src,join(dir,'invalid.wgsl'),...opts)
      assert.notEqual(result.status,0,'accepted invalid options '+opts.join(' '))
    }
    const nested=join(dir,'nested','new','default.wgsl')
    const out=invoke('shader',src,nested,'--stage=compute')
    assert.equal(out.status,0,out.stderr)
    assert.match(await readFile(nested,'utf8'),/@compute/)
  })
})
test('shader identifiers are hygienic; unsafe constructs leave output intact',async()=>{
  await inTemp(async dir=>{
    const accepted=[
      ['keywords',[
        'export function shade(var: u32): u32 {',
        '  const for: u32 = var + 1 as u32',
        '  if for > 100 as u32 { return 100 as u32 }',
        '  return for','}'
      ].join('\n'),/nq_arg0: u32/],
      ['boolean',[
        'export function shade(value: u32): u32 {',
        '  const ok: bool = value > 10 as u32 && value < 20 as u32',
        '  if ok { return 1 as u32 }',
        '  return 0 as u32','}'
      ].join('\n'),/let nq_local0: bool/]
    ]
    for(const [name,source,expected] of accepted){
      const file=join(dir,name+'.noqeri'),output=join(dir,name+'.wgsl')
      await writeFile(file,source+'\n')
      const result=invoke('shader',file,output,'--stage=compute')
      assert.equal(result.status,0,result.stderr)
      const wgsl=await readFile(output,'utf8')
      assert.match(wgsl,expected)
      assert.doesNotMatch(wgsl,/fn noqeri_shade\(var: u32\)/)
    }
    const rejected=[
      ['incomplete',['export function shade(value: u32): u32 {',
        '  if value > 10 as u32 { return 1 as u32 }','}'].join('\n')],
      ['unsafeCall','export function shade(value: u32): u32 { return clockMillis() as u32 }'],
      ['mutableCapture',['let captured: u32 = 1 as u32',
        'export function shade(value: u32): u32 { return captured }'].join('\n')],
      ['divideZero','export function shade(value: u32): u32 { return value / 0 as u32 }'],
      ['illegalFloatRemainder','export function shade(value: float): float { return value % 2.0 }'],
      ['loop',['export function shade(value: u32): u32 {',
        '  let cursor: u32 = 1 as u32',
        '  while cursor < 3 as u32 { cursor = cursor + 1 as u32 }',
        '  return cursor','}'].join('\n')],
      ['floatUnsignedMismatch','export function shade(value: float): float { return value + 1 as u32 }'],
      ['assignment',['export function shade(value: u32): u32 {',
        '  let total: u32 = value','  total = total + 1 as u32',
        '  return total','}'].join('\n')]
    ]
    for(const [name,source] of rejected){
      const file=join(dir,name+'.noqeri'),output=join(dir,name+'.wgsl')
      await writeFile(file,source+'\n')
      await writeFile(output,'previously-compiled-good-shader')
      const result=invoke('shader',file,output,'--stage=compute')
      assert.notEqual(result.status,0,'accepted unsafe shader '+name)
      assert.equal(await readFile(output,'utf8'),'previously-compiled-good-shader',
        'invalid compile overwrote last known good output '+name)
    }
  })
})
