import test from 'node:test'
import assert from 'node:assert/strict'
import {mkdtemp,rm,writeFile,mkdir} from 'node:fs/promises'
import {tmpdir} from 'node:os'
import {join} from 'node:path'
import {pathToFileURL} from 'node:url'
import {compileWeb,compileWebFile} from '../Runtime/web-compiler.mjs'
async function moduleOf(src,inspect){
  const dir=await mkdtemp(join(tmpdir(),'noqeri-esm-'))
  try {
    const file=join(dir,'output.mjs')
    await writeFile(file,compileWeb(src))
    return await inspect(await import(pathToFileURL(file).href+'?t='+Date.now()))
  } finally { await rm(dir,{recursive:true,force:true}) }
}
test('exported function and argument',()=>moduleOf('module smoke.exports\nexport function greeting(name: string): string { return "hello " + name }',m=>assert.equal(m.greeting('Ada'),'hello Ada')))
test('integer arithmetic',()=>moduleOf('export function add(a: int,b: int): int { return a+b }',m=>assert.equal(m.add(8,13),21)))
test('optional annotations and unparenthesized control flow',()=>moduleOf('export function max(a: int,b: int): int { let best = a if b > a { best = b } return best }',m=>assert.equal(m.max(4,9),9)))
test('arrays, indexing, slices and casts',()=>moduleOf('export function value(): int { let xs: [u8; 3] = [1 as u8, 2 as u8, 3 as u8] let view: []u8 = slice(xs) view[1] = 9 as u8 return view[1] }',m=>assert.equal(m.value(),9)))
test('external browser host bridge',async()=>{
  const original=globalThis.NoqeriHost
  try {globalThis.NoqeriHost={domText:(selector,text)=>selector==='#app'&&text==='Hi'}
    await moduleOf('extern function domText(selector: string, text: string): bool\nexport function render(): bool { return domText("#app","Hi") }',m=>assert.equal(m.render(),true))
  } finally { if(original===undefined)delete globalThis.NoqeriHost; else globalThis.NoqeriHost=original }
})
test('reject dynamic execution and native-only calls',()=>{
  assert.throws(()=>compileWeb('export function run(): int { return eval("1") }'),/invalid identifier|unsafe/i)
  assert.throws(()=>compileWeb('export function run(): int { return asm("nop") }'),/native-only/)
})
test('compiles local import graph into one module',async()=>{
 const root=await mkdtemp(join(tmpdir(),'nqri-graph-'))
 try {
  await writeFile(join(root,'math.nqr'),'function twice(x: int): int { return x * 2 }')
  await writeFile(join(root,'app.nqr'),'import "./math.nqr"\nexport function answer(): int { return twice(21) }')
  const out=join(root,'app.mjs'); await compileWebFile(join(root,'app.nqr'),out)
  const mod=await import(pathToFileURL(out).href+'?t='+Date.now())
  assert.equal(mod.answer(),42)
 } finally {await rm(root,{recursive:true,force:true})}
})
