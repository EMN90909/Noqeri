#!/usr/bin/env node
// Static web project builder with a restricted Noqeri-to-ES-module compiler.
// This compiler accepts a deliberately limited browser-safe language subset.
import { cp, lstat, mkdir, readFile, readdir, realpath, writeFile } from 'node:fs/promises'
import { resolve, relative, join, dirname, extname, sep } from 'node:path'
import { compileWeb } from './web-compiler.mjs'

const extensions = new Set(['.html','.htm','.css','.js','.mjs','.json','.svg','.png','.jpg','.jpeg','.gif','.webp','.ico','.woff','.woff2','.txt','.webmanifest'])
function inside(root, file) {
  const rel = relative(root, file)
  return rel === '' || (rel !== '..' && !rel.startsWith('..'+sep))
}
export async function buildWebProject({source='web',out='dist',checkSource=null}={}) {
  if (checkSource !== null && typeof checkSource !== 'function') throw new TypeError('checkSource must be a function')
  const input = await realpath(resolve(source))
  const output = resolve(out)
  if (inside(input,output) || inside(output,input)) throw Error('source and output directories must not contain one another')
  // Noqeri-only app: main.noqeri owns markup, state, components and events.
  // The view compiler is a bounded separate frontend until Stage-2 NIR parity.
  let viewSource=null
  try{viewSource=await readFile(join(input,'main.noqeri'),'utf8')}
  catch(error){if(error.code!=='ENOENT')throw error}
  if(viewSource!==null){
    const {isViewSource}=await import('./web-view-compiler.mjs')
    if(isViewSource(viewSource)){
      const {buildViewProject}=await import('./web-view-project.mjs')
      return buildViewProject({source:input,out:output,checkSource})
    }
  }
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
      if(extension==='.noqeri') {
        const target=join(output,relative(input,p).replace(/\.noqeri$/i,'.mjs'))
        if (entries.some(existing=>relative(input,existing).replace(/\.noqeri$/i,'.mjs')===relative(output,target))) throw Error('duplicate output asset: '+relative(output,target))
        entries.push(p)
        continue
      }
      if(!extensions.has(extension)) throw Error('unsupported web asset extension: '+relative(input,p))
      entries.push(p)
    }
  }
  await walk(input)
  const targets=new Set()
  for(const p of entries) {
    const rel=relative(input,p)
    const dest=extname(p).toLowerCase()==='.noqeri'?rel.replace(/\.noqeri$/i,'.mjs'):rel
    if(targets.has(dest)) throw Error('duplicate output asset: '+dest)
    targets.add(dest)
  }
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
    if(extname(file).toLowerCase()==='.noqeri') {
      const sourceText=await readFile(file,'utf8')
      if (checkSource) await checkSource(file,new TextEncoder().encode(sourceText))
      const compiled=compileWeb(sourceText)
      await writeFile(target.replace(/\.noqeri$/i,'.mjs'),compiled)
    } else if(['.html','.htm'].includes(extname(file).toLowerCase())) {
      const html=await readFile(file,'utf8')
      const rewritten=html.replace(/\b(src|href)(\s*=\s*["'])([^"']+)(["'])/gi,(full,attr,equals,ref,quote)=>{
        if (/^(?:[a-z][a-z\d+.-]*:|\/\/|#)/i.test(ref)) return full
        const [path,suffix='']=ref.split(/([?#].*)/,2)
        return /\.noqeri$/i.test(path)?attr+equals+path.replace(/\.noqeri$/i,'.mjs')+suffix+quote:full
      })
      await writeFile(target,rewritten)
    } else await cp(file,target,{force:true,errorOnExist:false})
  }
  return {files:entries.length,html:html.length,compiled:entries.filter(p=>extname(p).toLowerCase()==='.noqeri').length,source:input,output}
}
if(process.argv[1] && resolve(process.argv[1]) === resolve(new URL(import.meta.url).pathname)) {
  const [source='web',out='dist']=process.argv.slice(2)
  buildWebProject({source,out}).then(r=>console.log('Built '+r.files+' assets ('+r.html+' HTML) into '+r.output)).catch(e=>{console.error('noqeri web-project:',e.message);process.exitCode=1})
}
