import test from 'node:test'
import assert from 'node:assert/strict'
import {mkdtemp,rm,writeFile} from 'node:fs/promises'
import {tmpdir} from 'node:os'
import {join} from 'node:path'
import {pathToFileURL} from 'node:url'
import {compileWeb} from '../Runtime/web-compiler.mjs'
async function moduleOf(src,inspect){
  const dir=await mkdtemp(join(tmpdir(),'noqeri-esm-'))
  try {
    const file=join(dir,'output.mjs')
    await writeFile(file,compileWeb(src))
    return await inspect(await import(pathToFileURL(file).href))
  } finally { await rm(dir,{recursive:true,force:true}) }
}
test('exported function and argument',()=>moduleOf('module smoke.exports\nexport function greeting(name: string): string { return "hello " + name }',m=>assert.equal(m.greeting('Ada'),'hello Ada')))
test('integer arithmetic',()=>moduleOf('export function add(a: int,b: int): int { return a+b }',m=>assert.equal(m.add(8,13),21)))
test('JSON string return',()=>moduleOf('export function response(): string { return "{\\"status\\":\\"ok\\"}" }',m=>assert.equal(JSON.parse(m.response()).status,'ok')))
test('external browser host bridge',async()=>{
  const original=globalThis.NoqeriHost
  try {globalThis.NoqeriHost={domText:(selector,text)=>selector==='#app'&&text==='Hi'}
    await moduleOf('extern function domText(selector: string, text: string): bool\nexport function render(): bool { return domText("#app","Hi") }',m=>assert.equal(m.render(),true))
  } finally { if(original===undefined)delete globalThis.NoqeriHost; else globalThis.NoqeriHost=original }
})
test('reject dynamic execution and global access',()=>{
  assert.throws(()=>compileWeb('export function run(): int { return eval("1") }'),/Unsafe/)
  assert.throws(()=>compileWeb('export function run(): int { return globalThis }'),/Unsafe/)
})
test('reject unimplemented imports and native-only calls',()=>{
  assert.throws(()=>compileWeb('import "foo.nqr"'),/Expected function/)
  assert.throws(()=>compileWeb('export function run(): int { return asm("nop") }'),/Unsafe/)
})
test('reject invalid syntax and duplicate exports',()=>{
  assert.throws(()=>compileWeb('export function run(): int { return 1'),/Unterminated/)
  assert.throws(()=>compileWeb('export function run(): int { return 1 } export function run(): int { return 2 }'),/Duplicate/)
})
