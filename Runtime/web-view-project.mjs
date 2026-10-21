// First-generation Noqeri-only web app builder. Does not silently treat
// the bounded view DSL as Stage-2 typed NIR or auto-install npm packages.
import {lstat, mkdir, readFile, readdir, realpath, copyFile, writeFile} from 'node:fs/promises'
import {resolve,relative,join,dirname,extname,sep} from 'node:path'
import {fileURLToPath} from 'node:url'
import {compileView,isViewSource,parseView} from './web-view-compiler.mjs'
import {compileWeb} from './web-compiler.mjs'
const ASSETS=new Set('.css .html .htm .js .mjs .json .svg .png .jpg .jpeg .gif .webp .ico .woff .woff2 .txt .webmanifest'.split(' '))
const under=(root,path)=>{const rel=relative(root,path);return rel===''||(rel!=='..'&&!rel.startsWith('..'+sep)&&!rel.startsWith(sep))}
export async function buildViewProject({source,out,checkSource=null}={}) {
 if(checkSource!==null && typeof checkSource!=='function')throw TypeError('checkSource must be a function')
 const input=await realpath(resolve(source)),output=resolve(out)
 if(under(input,output)||under(output,input))throw Error('source and output directories must not contain one another')
 const all=[];
 async function walk(dir){
  for(const entry of await readdir(dir,{withFileTypes:true})){
   if(entry.name==='node_modules'||entry.name==='.git')continue
   const path=join(dir,entry.name),st=await lstat(path)
   if(st.isSymbolicLink())throw Error('Noqeri web: symlinks forbidden: '+path)
   if(st.isDirectory()){await walk(path);continue}
   if(!st.isFile()||(!ASSETS.has(extname(path).toLowerCase())&&extname(path).toLowerCase()!=='.noqeri'))throw Error('unsupported web asset: '+path)
   all.push(path)
  }
 }
 await walk(input)
 const entries=new Map(),viewAst=new Map(),modules=new Set();
 for(const file of all.filter(x=>x.endsWith('.noqeri'))){
  const code=await readFile(file,'utf8');entries.set(file,code)
  if(isViewSource(code))viewAst.set(file,parseView(code))
 }
 const main=join(input,'main.noqeri'),mainView=viewAst.get(main)?.view;
 if(!mainView)throw Error('Noqeri view project requires main.noqeri containing a view')
 const htmlFiles=all.filter(p=>/\.(?:html|htm)$/.test(p))
 const outputPaths=new Set();
 for(const file of all){
  const rel=relative(input,file),target=rel.endsWith('.noqeri')?rel.replace(/\.noqeri$/,'.mjs'):rel;
  if(outputPaths.has(target))throw Error('duplicate output asset: '+target)
  outputPaths.add(target)
 }
 if(outputPaths.has('__noqeri/runtime.mjs'))throw Error('reserved runtime asset path __noqeri/runtime.mjs')
 const mapFile=join(input,'web.importmap.json')
 let importmap=null
 if(all.includes(mapFile)){
  const raw=await readFile(mapFile,'utf8')
  if(raw.length>64*1024)throw Error('importmap exceeds 64KiB')
  importmap=JSON.parse(raw)
  if(!importmap||typeof importmap!=='object'||Array.isArray(importmap)||!importmap.imports||typeof importmap.imports!=='object'||Array.isArray(importmap.imports))throw Error('web.importmap.json requires imports object')
  for(const [name,url] of Object.entries(importmap.imports)){
   if(!/^[a-zA-Z0-9@][a-zA-Z0-9@._/-]*$/.test(name)||name.includes('..')||typeof url!=='string'||!(/^(?:https:\/\/|\/(?!\/)|\.\/)/.test(url)))throw Error('unsafe web import map: '+name)
   if(url.startsWith('./')||url.startsWith('/')){
    const p=resolve(input,url.replace(/^\.?\//,''))
    if(!under(input,p)||!all.includes(p))throw Error('missing import map target: '+url)
   }
  }
 }
 for(const [file,ast] of viewAst){
  for(const i of ast.imports){
   if(i.kind==='js'){
    if(i.specifier.startsWith('.')){
     const path=resolve(dirname(file),i.specifier)
     if(!under(input,path)||!all.includes(path))throw Error('missing or unsafe JavaScript module: '+i.specifier)
    } else if(!Object.hasOwn(importmap?.imports||{},i.specifier))
     throw Error('import js "'+i.specifier+'" needs explicit web.importmap.json mapping')
   }else{
    const path=resolve(dirname(file),i.specifier+'.noqeri')
    if(!under(input,path)||!viewAst.has(path))throw Error('Noqeri UI package missing local component source: '+i.specifier)
    modules.add(path)
   }
  }
 }
 const mapScript=importmap?'<script type="importmap">'+JSON.stringify(importmap).replace(/</g,'\\u003c')+'</script>':''
 await mkdir(output,{recursive:true})
 for(const file of all){
  const rel=relative(input,file),destination=join(output,rel)
  await mkdir(dirname(destination),{recursive:true})
  if(file.endsWith('.noqeri')){
   const sourceText=entries.get(file)
   if(checkSource&&!viewAst.has(file))await checkSource(file,new TextEncoder().encode(sourceText))
   const result=relative(dirname(rel.replace(/\.noqeri$/,'.mjs')),'__noqeri/runtime.mjs').replaceAll('\\','/')
   const runtimeImport=result.startsWith('.')?result:'./'+result
   const compiled=viewAst.has(file)?compileView(sourceText,{runtimeImport}):compileWeb(sourceText)
   await writeFile(destination.replace(/\.noqeri$/,'.mjs'),compiled)
  }else if(file.endsWith('.html')||file.endsWith('.htm')){
   let html=await readFile(file,'utf8')
   html=html.replace(/\b(src|href)(\s*=\s*["'])([^"']+)(["'])/gi,(all,attr,eq,ref,q)=>{
    if(/^[a-z][a-z0-9+.-]*:|^\/\//i.test(ref)||ref.startsWith('#'))return all
    const [path,suffix='']=ref.split(/([?#].*)/,2)
    const resolved=resolve(dirname(file),path)
    if(!under(input,resolved))throw Error('HTML reference escapes project: '+ref)
    return attr+eq+path.replace(/\.noqeri$/i,'.mjs')+suffix+q
   })
   if(mapScript){
    if(/<script\s+type=["']importmap["']/i.test(html))throw Error('duplicate browser importmap')
    const head=/<head[^>]*>/i
    html=head.test(html)?html.replace(head,text=>text+mapScript):mapScript+html
   }
   await writeFile(destination,html)
  } else await copyFile(file,destination)
 }
 await mkdir(join(output,'__noqeri'),{recursive:true})
 await copyFile(fileURLToPath(new URL('./noqeri-web-runtime.mjs',import.meta.url)),join(output,'__noqeri/runtime.mjs'))
 if(!htmlFiles.length){
  const css=all.includes(join(input,'styles.css'))?'<link rel="stylesheet" href="./styles.css">':''
  const html='<!doctype html>\n<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Noqeri web</title>'+css+mapScript+'</head><body><main id="app"></main><script type="module" src="./main.mjs"></script></body></html>\n'
  await writeFile(join(output,'index.html'),html)
 }
 return {files:all.length+(htmlFiles.length?0:1)+1,html:Math.max(1,htmlFiles.length),compiled:entries.size,views:viewAst.size,source:input,output}
}
