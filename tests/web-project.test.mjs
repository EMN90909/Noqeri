import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtemp, mkdir, readFile, symlink, writeFile, rm, access } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { pathToFileURL } from 'node:url'
import { buildWebProject } from '../Runtime/web-project.mjs'
async function fixture(fn){const root=await mkdtemp(join(tmpdir(),'nqr-project-'));try{return await fn(root)}finally{await rm(root,{recursive:true,force:true})}}
test('packages HTML, CSS, JS and subfolders',()=>fixture(async root=>{
 const src=join(root,'src'),dist=join(root,'dist');await mkdir(join(src,'assets'),{recursive:true})
 await writeFile(join(src,'index.html'),'<link href="assets/site.css"><script src="assets/app.js"></script>')
 await writeFile(join(src,'assets/site.css'),'body { color: red }');await writeFile(join(src,'assets/app.js'),'document.body.dataset.ok = "yes"')
 const result=await buildWebProject({source:src,out:dist});assert.equal(result.files,3);assert.equal(result.compiled,0)
 assert.equal(await readFile(join(dist,'assets/site.css'),'utf8'),'body { color: red }')
}))
test('compiles .nqr and rewrites HTML to a module script',()=>fixture(async root=>{
 const src=join(root,'src'),dist=join(root,'dist');await mkdir(src)
 await writeFile(join(src,'index.html'),'<div id="app"></div><script src="app.nqr"></script>')
 await writeFile(join(src,'app.nqr'),'export function answer(): int { return 42 }')
 let validated=0
 const result=await buildWebProject({source:src,out:dist,validateNoqeri:async()=>{validated++}})
 assert.equal(result.compiled,1);assert.equal(validated,1)
 const html=await readFile(join(dist,'index.html'),'utf8');assert.match(html,/type="module"/);assert.match(html,/src="app\.js"/)
 await access(join(dist,'app.js'));const mod=await import(pathToFileURL(join(dist,'app.js')).href+'?t='+Date.now());assert.equal(mod.answer(),42)
}))
test('compiles local Noqeri imports into the entry module',()=>fixture(async root=>{
 const src=join(root,'src'),dist=join(root,'dist');await mkdir(src)
 await writeFile(join(src,'index.html'),'<script type="module" src="app.nqr"></script>')
 await writeFile(join(src,'math.nqr'),'function add(a: int,b: int): int { return a+b }')
 await writeFile(join(src,'app.nqr'),'import "./math.nqr"\nexport function answer(): int { return add(20,22) }')
 await buildWebProject({source:src,out:dist})
 const mod=await import(pathToFileURL(join(dist,'app.js')).href+'?t='+Date.now());assert.equal(mod.answer(),42)
}))
test('rejects Noqeri imports that escape the web project',()=>fixture(async root=>{
 const src=join(root,'src'),dist=join(root,'dist');await mkdir(src)
 await writeFile(join(root,'secret.nqr'),'function secret(): int { return 7 }')
 await writeFile(join(src,'index.html'),'<script src="app.nqr"></script>')
 await writeFile(join(src,'app.nqr'),'import "../secret.nqr"\nexport function x(): int { return secret() }')
 await assert.rejects(buildWebProject({source:src,out:dist}),/import escapes web project/)
}))
test('rejects path traversal and symlinks',()=>fixture(async root=>{
 const src=join(root,'src');await mkdir(src);await writeFile(join(src,'index.html'),'<script src="../secret.js"></script>')
 await assert.rejects(buildWebProject({source:src,out:join(root,'dist')}),/escapes project/)
 await writeFile(join(src,'index.html'),'hello');await symlink('/etc/passwd',join(src,'secret.txt'));await assert.rejects(buildWebProject({source:src,out:join(root,'dist')}),/symlinks/)
}))
test('rejects nested output and requires HTML',()=>fixture(async root=>{
 const src=join(root,'src');await mkdir(src);await writeFile(join(src,'index.html'),'ok');await assert.rejects(buildWebProject({source:src,out:join(src,'dist')}),/must not contain/)
 await rm(src,{recursive:true});await mkdir(src);await writeFile(join(src,'app.nqr'),'export function x(): int { return 1 }');await assert.rejects(buildWebProject({source:src,out:join(root,'dist2')}),/at least one HTML/)
}))
