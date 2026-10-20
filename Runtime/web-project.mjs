#!/usr/bin/env node
import { cp, lstat, mkdir, readFile, readdir, realpath, writeFile } from 'node:fs/promises'
import { resolve, relative, join, dirname, extname, sep } from 'node:path'
import { compileWebFile } from './web-compiler.mjs'

const assetExtensions = new Set(['.html','.htm','.css','.js','.mjs','.json','.svg','.png','.jpg','.jpeg','.gif','.webp','.ico','.woff','.woff2','.txt','.webmanifest'])
function inside(root,file){const rel=relative(root,file);return rel===''||(rel!=='..'&&!rel.startsWith('..'+sep))}
function webOutputFor(inputRoot,outputRoot,file){const rel=relative(inputRoot,file);return join(outputRoot,rel.replace(/\.nqr$/i,'.js'))}
function rewriteNqrScripts(html) {
  return html.replace(/<script\b([^>]*?)\bsrc\s*=\s*(["'])([^"']+\.nqr(?:[?#][^"']*)?)\2([^>]*)><\/script>/gi,(all,before,q,src,after)=>{
    const next=src.replace(/\.nqr(?=([?#]|$))/i,'.js')
    const attrs=(before+after).replace(/\s+type\s*=\s*(["'])[^"']*\1/gi,'')
    return `<script${attrs} type="module" src=${q}${next}${q}></script>`
  })
}

export async function buildWebProject({source='web',out='dist',compileNoqeri=compileWebFile,validateNoqeri=null,resolvePackage=null}={}) {
  const input=await realpath(resolve(source)), output=resolve(out)
  if(inside(input,output)||inside(output,input))throw Error('source and output directories must not contain one another')
  const entries=[]
  async function walk(dir){
    for(const entry of await readdir(dir,{withFileTypes:true})){
      if(entry.name==='.git'||entry.name==='node_modules')continue
      const p=join(dir,entry.name),stat=await lstat(p)
      if(stat.isSymbolicLink())throw Error('symlinks are not supported: '+p)
      if(stat.isDirectory()){await walk(p);continue}
      if(!stat.isFile())throw Error('unsupported filesystem entry: '+p)
      const extension=extname(p).toLowerCase()
      if(extension!=='.nqr'&&!assetExtensions.has(extension))throw Error('unsupported web asset extension: '+relative(input,p))
      entries.push(p)
    }
  }
  await walk(input)
  const html=entries.filter(p=>['.html','.htm'].includes(extname(p).toLowerCase()))
  if(!html.length)throw Error('web project requires at least one HTML file')
  for(const file of html){
    const data=await readFile(file,'utf8')
    for(const match of data.matchAll(/\b(?:src|href)\s*=\s*["']([^"']+)["']/gi)){
      const ref=match[1]
      if(/^(?:https?:|data:|mailto:|tel:|#|\/\/)/i.test(ref)||ref.startsWith('/'))continue
      const clean=decodeURIComponent(ref.split(/[?#]/)[0])
      if(!inside(input,resolve(dirname(file),clean)))throw Error('HTML reference escapes project: '+ref)
    }
  }
  await mkdir(output,{recursive:true})
  let compiled=0,copied=0
  for(const file of entries){
    const extension=extname(file).toLowerCase()
    if(extension==='.nqr'){
      if(typeof validateNoqeri==='function')await validateNoqeri(file)
      const target=webOutputFor(input,output,file)
      await compileNoqeri(file,target,{resolvePackage,root:input})
      compiled++
      continue
    }
    const target=join(output,relative(input,file));await mkdir(dirname(target),{recursive:true})
    if(extension==='.html'||extension==='.htm'){
      await writeFile(target,rewriteNqrScripts(await readFile(file,'utf8')))
    } else await cp(file,target,{force:true,errorOnExist:false})
    copied++
  }
  return {files:entries.length,html:html.length,compiled,copied,source:input,output}
}

if(process.argv[1]&&resolve(process.argv[1])===resolve(new URL(import.meta.url).pathname)){
  const [source='web',out='dist']=process.argv.slice(2)
  buildWebProject({source,out}).then(r=>console.log(`Built ${r.files} web inputs (${r.compiled} Noqeri modules) into ${r.output}`)).catch(e=>{console.error('noqeri web-project:',e.message);process.exitCode=1})
}
