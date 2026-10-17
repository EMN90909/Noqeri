#!/usr/bin/env node
// Static web assets only. This is NOT a Noqeri-to-JavaScript compiler.
import { cp, lstat, mkdir, readFile, readdir, realpath } from 'node:fs/promises'
import { resolve, relative, join, dirname, extname, sep } from 'node:path'

const extensions = new Set(['.html','.htm','.css','.js','.mjs','.json','.svg','.png','.jpg','.jpeg','.gif','.webp','.ico','.woff','.woff2','.txt','.webmanifest'])
function inside(root, file) {
  const rel = relative(root, file)
  return rel === '' || (rel !== '..' && !rel.startsWith('..'+sep))
}
export async function buildWebProject({source='web',out='dist'}={}) {
  const input = await realpath(resolve(source))
  const output = resolve(out)
  if (inside(input,output) || inside(output,input)) throw Error('source and output directories must not contain one another')
  const entries=[]
  async function walk(dir) {
    for (const entry of await readdir(dir,{withFileTypes:true})) {
      if (entry.name === '.git' || entry.name === 'node_modules') continue
      const p=join(dir,entry.name)
      const stat=await lstat(p)
      if(stat.isSymbolicLink()) throw Error('symlinks are not supported: '+p)
      if(stat.isDirectory()) { await walk(p); continue }
      if(!stat.isFile()) throw Error('unsupported filesystem entry: '+p)
      const extension=extname(p).toLowerCase()
      if(extension==='.nqr') throw Error('Noqeri .nqr compilation unavailable in the current Stage-2 CLI: '+relative(input,p))
      if(!extensions.has(extension)) throw Error('unsupported web asset extension: '+relative(input,p))
      entries.push(p)
    }
  }
  await walk(input)
  const html=entries.filter(p=>['.html','.htm'].includes(extname(p).toLowerCase()))
  if(!html.length) throw Error('web project requires at least one HTML file')
  for(const file of html) {
    const data=await readFile(file,'utf8')
    for(const match of data.matchAll(/\b(?:src|href)\s*=\s*["']([^"']+)["']/gi)) {
      const ref=match[1]
      if(/^(?:https?:|data:|mailto:|tel:|#|\/\/)/i.test(ref)) continue
      if(ref.startsWith('/')) continue
      const clean=decodeURIComponent(ref.split(/[?#]/)[0])
      if(!inside(input,resolve(dirname(file),clean))) throw Error('HTML reference escapes project: '+ref)
    }
  }
  await mkdir(output,{recursive:true})
  for(const file of entries) {
    const target=join(output,relative(input,file))
    await mkdir(dirname(target),{recursive:true})
    await cp(file,target,{force:true,errorOnExist:false})
  }
  return {files:entries.length,html:html.length,source:input,output}
}
if(process.argv[1] && resolve(process.argv[1]) === resolve(new URL(import.meta.url).pathname)) {
  const [source='web',out='dist']=process.argv.slice(2)
  buildWebProject({source,out}).then(r=>console.log('Built '+r.files+' assets ('+r.html+' HTML) into '+r.output)).catch(e=>{console.error('noqeri web-project:',e.message);process.exitCode=1})
}
