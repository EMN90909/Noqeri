import test from 'node:test'
import assert from 'node:assert/strict'
import {mkdtemp,mkdir,writeFile,readFile,rm} from 'node:fs/promises'
import {join} from 'node:path'
import {tmpdir} from 'node:os'
import {pathToFileURL} from 'node:url'
import {parseView,compileView,isViewSource} from '../Runtime/web-view-compiler.mjs'
import {compileTheme} from '../Runtime/noqeri-web-theme.mjs'
import {createRouter} from '../Runtime/noqeri-web-runtime.mjs'
import {buildWebProject} from '../Runtime/web-project.mjs'

test('Noqeri routes and theme are parsed and compiled without user-authored JavaScript',()=>{
  const source='theme { primary: "#123abc" radius: 9 }\nroute "/" { h1 { "Home" } }\nroute "/about" { h1 { "About" } }\nview { p { "Missing" } }'
  assert.equal(isViewSource(source),true)
  const ast=parseView(source)
  assert.deepEqual(ast.routes.map(x=>x.path),['/','/about'])
  assert.equal(ast.theme.primary,'#123abc')
  const compiled=compileView(source)
  assert.match(compiled,/createRouter as __createRouter/)
  assert.match(compiled,/__path==="\/about"/)
  assert.doesNotMatch(compiled,/\beval\s*\(/)
})
test('route paths and theme tokens fail closed',()=>{
  for(const source of [
    'route "//evil" { p { "x" } }',
    'route "/../admin" { p { "x" } }',
    'route "/" { p { "x" } } route "/" { p { "y" } }',
    'theme { something: "#ffffff" } route "/" { p { "x" } }',
    'theme { primary: window.location } route "/" { p { "x" } }',
    'function navigate() { return 1 } route "/" { p { "x" } }'
  ]) assert.throws(()=>compileView(source),/Noqeri view|forbidden|undeclared/i,source)
  assert.throws(()=>compileTheme({primary:'red; color:blue'}),/hex color/)
  assert.throws(()=>compileTheme({radius:900}),/0..32/)
  assert.match(compileTheme({primary:'#125bad'}),/--nq-primary: #125bad/)
})
test('browser router permits same-origin links and restores history without leaks',()=>{
  const original=globalThis.window
  const listeners=new Map(),rootEvents=new Map()
  const win={
    location:{pathname:'/'},
    history:{pushState(_s,_t,path){win.location.pathname=path}},
    addEventListener(type,fn){listeners.set(type,fn)},
    removeEventListener(type){listeners.delete(type)}
  }
  const root={
    addEventListener(type,fn){rootEvents.set(type,fn)},
    removeEventListener(type){rootEvents.delete(type)},
    contains(){return true}
  }
  try{
    globalThis.window=win
    let renders=0
    const router=createRouter(root,()=>renders++)
    assert.equal(router.path(),'/')
    assert.equal(router.navigate('/about'),true)
    assert.equal(router.path(),'/about')
    assert.equal(router.navigate('//evil.test'),false)
    assert.equal(router.navigate('/../admin'),false)
    assert.equal(renders,1)
    win.location.pathname='/';listeners.get('popstate')()
    assert.equal(router.path(),'/')
    assert.equal(renders,2)
    let prevented=false
    const anchor={hasAttribute(){return false},getAttribute(key){return key==='href'?'/about':null}}
    rootEvents.get('click')({button:0,target:{closest(){return anchor}},preventDefault(){prevented=true}})
    assert.equal(prevented,true)
    assert.equal(router.path(),'/about')
    router.dispose()
    assert.equal(listeners.size,0)
    assert.equal(rootEvents.size,0)
    assert.equal(router.navigate('/'),false)
  }finally{if(original===undefined)delete globalThis.window;else globalThis.window=original}
})
test('build emits standalone deep-link pages, native component module and CSS',async()=>{
  const temp=await mkdtemp(join(tmpdir(),'noqeri-framework-'))
  try{
    const src=join(temp,'src'),out=join(temp,'dist')
    await mkdir(src)
    await writeFile(join(src,'main.noqeri'),'import noqeri/ui as ui\ntheme { accent: "#abcdef" }\nroute "/" { ui.Heading("Home") }\nroute "/about" { ui.Card("About", "No JS") }')
    const result=await buildWebProject({source:src,out})
    assert.equal(result.html,2)
    assert.match(await readFile(join(out,'index.html'),'utf8'),/src="\.\/main\.mjs"/)
    assert.match(await readFile(join(out,'about/index.html'),'utf8'),/src="\.\.\/main\.mjs"/)
    assert.match(await readFile(join(out,'__noqeri/theme.css'),'utf8'),/--nq-accent: #abcdef/)
    const ui=await readFile(join(out,'noqeri/ui.mjs'),'utf8')
    assert.match(ui,/export function Card/)
    const compiled=await import(pathToFileURL(join(out,'main.mjs')).href)
    assert.equal(typeof compiled.mount,'function')
    assert.match(JSON.stringify(compiled.render()),/Home/)
  }finally{await rm(temp,{recursive:true,force:true})}
})
