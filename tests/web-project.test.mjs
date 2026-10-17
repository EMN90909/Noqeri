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
test('fails closed on .nqr until a real compiler backend exists',()=>fixture(async root=>{
 const src=join(root,'src'); await mkdir(src)
 await writeFile(join(src,'index.html'),'hello')
 await writeFile(join(src,'app.nqr'),'function main(): int { return 0 }')
 await assert.rejects(buildWebProject({source:src,out:join(root,'dist')}),/compilation unavailable/)
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
