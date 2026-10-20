import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtemp, mkdir, readFile, symlink, writeFile, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { buildWebProject } from '../Runtime/web-project.mjs'

async function fixture(fn) {
  const root=await mkdtemp(join(tmpdir(),'nqr-project-'))
  try { return await fn(root) } finally { await rm(root,{recursive:true,force:true}) }
}
test('packages HTML, CSS, JS and subfolders',()=>fixture(async root=>{
 const src=join(root,'src'),dist=join(root,'dist')
 await mkdir(join(src,'assets'),{recursive:true})
 await writeFile(join(src,'index.html'),'<link href="assets/site.css"><script src="assets/app.js"></script>')
 await writeFile(join(src,'assets/site.css'),'body { color: red }')
 await writeFile(join(src,'assets/app.js'),'document.body.dataset.ok = "yes"')
 const result=await buildWebProject({source:src,out:dist})
 assert.equal(result.files,3)
 assert.equal(await readFile(join(dist,'assets/site.css'),'utf8'),'body { color: red }')
 assert.match(await readFile(join(dist,'index.html'),'utf8'),/app.js/)
}))
test('compiles .noqeri modules and rewrites HTML references',()=>fixture(async root=>{
 const src=join(root,'src'); await mkdir(src)
 await writeFile(join(src,'index.html'),'<script type="module" src="app.noqeri"></script>')
 await writeFile(join(src,'app.noqeri'),'export function main(): int { return 42 }')
 const dist=join(root,'dist')
 const result=await buildWebProject({source:src,out:dist})
 assert.equal(result.compiled,1)
 assert.match(await readFile(join(dist,'index.html'),'utf8'),/src="app.mjs"/)
 assert.match(await readFile(join(dist,'app.mjs'),'utf8'),/export function main/)
 assert.equal((await import('node:url')).pathToFileURL!==undefined,true)
 const module=await import((await import('node:url')).pathToFileURL(join(dist,'app.mjs')).href)
 assert.equal(module.main(),42)
}))
test('rejects path traversal and symlinks',()=>fixture(async root=>{
 const src=join(root,'src');await mkdir(src)
 await writeFile(join(src,'index.html'),'<script src="../secret.js"></script>')
 await assert.rejects(buildWebProject({source:src,out:join(root,'dist')}),/escapes project/)
 await writeFile(join(src,'index.html'),'hello')
 await symlink('/etc/passwd',join(src,'secret.txt'))
 await assert.rejects(buildWebProject({source:src,out:join(root,'dist')}),/symlinks/)
}))
test('rejects nested output',()=>fixture(async root=>{
 const src=join(root,'src');await mkdir(src);await writeFile(join(src,'index.html'),'ok')
 await assert.rejects(buildWebProject({source:src,out:join(src,'dist')}),/must not contain/)
}))
test('requires HTML entry point',()=>fixture(async root=>{
 const src=join(root,'src');await mkdir(src);await writeFile(join(src,'app.js'),'ok')
 await assert.rejects(buildWebProject({source:src,out:join(root,'dist')}),/at least one HTML/)
}))

test('rejects unsupported Noqeri web constructs without silently copying source',()=>fixture(async root=>{
 const src=join(root,'src');await mkdir(src)
 await writeFile(join(src,'index.html'),'<script src="app.noqeri"></script>')
 await writeFile(join(src,'app.noqeri'),'export function run(): int { return eval("1") }')
 await assert.rejects(buildWebProject({source:src,out:join(root,'dist')}),/Unsafe/)
}))
test('rejects output collision between a Noqeri module and a prebuilt module',()=>fixture(async root=>{
 const src=join(root,'src');await mkdir(src)
 await writeFile(join(src,'index.html'),'ok')
 await writeFile(join(src,'app.noqeri'),'export function run(): int { return 1 }')
 await writeFile(join(src,'app.mjs'),'export const run = 1')
 await assert.rejects(buildWebProject({source:src,out:join(root,'dist')}),/duplicate output asset/)
}))

test('runs provided semantic validation for every Noqeri module',()=>fixture(async root=>{
 const src=join(root,'src'),dist=join(root,'dist');await mkdir(src)
 await writeFile(join(src,'index.html'),'<script type="module" src="main.noqeri"></script>')
 await writeFile(join(src,'main.noqeri'),'export function main(): int { return 1 }')
 const seen=[]
 await buildWebProject({source:src,out:dist,checkSource:async (file,bytes)=>{
   seen.push(file)
   assert.match(new TextDecoder().decode(bytes),/export function main/)
 }})
 assert.equal(seen.length,1)
 assert.match(seen[0],/main\.noqeri$/)
}))
test('fails closed when semantic validation fails',()=>fixture(async root=>{
 const src=join(root,'src');await mkdir(src)
 await writeFile(join(src,'index.html'),'<script src="main.noqeri"></script>')
 await writeFile(join(src,'main.noqeri'),'export function main(): int { return 1 }')
 await assert.rejects(buildWebProject({source:src,out:join(root,'dist'),checkSource:async()=>{throw Error('Stage-2 rejected source')}}),/Stage-2 rejected source/)
}))
