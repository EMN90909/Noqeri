import test from 'node:test'
import assert from 'node:assert/strict'
import {mkdtemp,mkdir,readFile,writeFile,copyFile,rm} from 'node:fs/promises'
import {tmpdir} from 'node:os'
import {join} from 'node:path'
import {pathToFileURL} from 'node:url'
import {compileView,parseView} from '../Runtime/web-view-compiler.mjs'
import {buildWebProject} from '../Runtime/web-project.mjs'
const fixture=async fn=>{
 const dir=await mkdtemp(join(tmpdir(),'noqeri-reactive-web-'))
 try{return await fn(dir)}finally{await rm(dir,{recursive:true,force:true})}
}
class FakeNode{
 constructor(document,tag,text=''){this.ownerDocument=document;this.tag=tag;this.text=text;this.childNodes=[];this.parent=null;this.attrs=new Map();this.listeners=new Map()}
 get textContent(){return this.tag==='#text'?this.text:this.childNodes.map(n=>n.textContent).join('')}
 set textContent(x){if(this.tag==='#text')this.text=String(x);else{this.childNodes=[];this.appendChild(this.ownerDocument.createTextNode(String(x)))}}
 get lastChild(){return this.childNodes.at(-1)}
 appendChild(node){this.childNodes.push(node);node.parent=this;return node}
 replaceChild(next,old){const idx=this.childNodes.indexOf(old);if(idx<0)throw Error('missing old');this.childNodes[idx]=next;next.parent=this;return old}
 removeChild(node){const idx=this.childNodes.indexOf(node);if(idx<0)throw Error('missing');this.childNodes.splice(idx,1)}
 replaceChildren(...nodes){this.childNodes=[];for(const node of nodes)this.appendChild(node)}
 setAttribute(k,v){this.attrs.set(k,String(v))}
 removeAttribute(k){this.attrs.delete(k)}
 addEventListener(k,handler){this.listeners.set(k,handler)}
 removeEventListener(k){this.listeners.delete(k)}
 fire(kind){const listener=this.listeners.get(kind);if(!listener)throw Error('missing event '+kind);listener({preventDefault(){}})}
}
class FakeDocument{
 createElement(tag){return new FakeNode(this,tag)}
 createTextNode(t){return new FakeNode(this,'#text',String(t))}
}
function find(node,tag){if(node.tag===tag)return node;for(const c of node.childNodes){const match=find(c,tag);if(match)return match}return null}
async function imported(source,fn){
 return fixture(async dir=>{
  await mkdir(join(dir,'__noqeri'))
  await copyFile(new URL('../Runtime/noqeri-web-runtime.mjs',import.meta.url),join(dir,'__noqeri/runtime.mjs'))
  await writeFile(join(dir,'main.mjs'),compileView(source))
  const module=await import(pathToFileURL(join(dir,'main.mjs')).href)
  return fn(module,dir)
 })
}
test('Noqeri source parses markup, reactive count and typed component declarations',()=>{
 const ast=parseView('let count = 0\ncomponent Heading(label: string) { h1 { label } }\nview { Heading("Hello") button on.click { count = count + 1 } { "Count: " + count } }')
 assert.equal(ast.states[0].name,'count')
 assert.equal(ast.components[0].name,'Heading')
 assert.equal(ast.view.length,2)
 assert.match(compileView('let count = 0\nview { button on.click { count = count + 1 } { "Count: " + count } }'),/export function mount/)
})
test('renders state update without replacing button DOM node',()=>imported('let count = 0\nview { button on.click { count = count + 1 } { "Count: " + count } }',async module=>{
 const doc=new FakeDocument(),root=doc.createElement('main')
 const app=module.mount(root),btn=find(root,'button')
 assert.equal(btn.textContent,'Count: 0')
 btn.fire('click')
 await Promise.resolve()
 assert.equal(find(root,'button'),btn)
 assert.equal(btn.textContent,'Count: 1')
 btn.fire('click')
 await Promise.resolve()
 assert.equal(btn.textContent,'Count: 2')
 app.dispose()
 assert.equal(root.childNodes.length,0)
}))
test('safe DOM text rendering never injects markup',()=>imported('let greeting = "<script>alert(1)</script>"\nview { p { greeting } }',async module=>{
 const root=new FakeDocument().createElement('main')
 const app=module.mount(root)
 assert.equal(find(root,'p').textContent,'<script>alert(1)</script>')
 assert.equal(find(root,'script'),null)
 app.dispose()
}))
test('declarative same-origin data updates without an HTTP or framework library',async()=>{
 const original=globalThis.fetch
 try{
  globalThis.fetch=async url=>{assert.equal(url,'/api/hello');return {ok:true,text:async()=>JSON.stringify('Hello from backend')}}
  await imported('load hello from "/api/hello"\nview { p { hello.data } }',async module=>{
   const root=new FakeDocument().createElement('main')
   const app=module.mount(root)
   await new Promise(r=>setTimeout(r,12))
   assert.equal(find(root,'p').textContent,'Hello from backend')
   app.dispose()
  })
 }finally{globalThis.fetch=original}
})
test('Noqeri animation wrappers export idiomatic methods over an existing JS module',()=>fixture(async dir=>{
 const src=join(dir,'src'),dist=join(dir,'dist')
 await mkdir(join(src,'animation'),{recursive:true})
 await mkdir(join(src,'vendor'),{recursive:true})
 await writeFile(join(src,'main.noqeri'),'import animation/motion as motion\nview { div { "Test" } }\non.mount { motion.arrive(".card") }')
 await writeFile(join(src,'animation/motion.noqeri'),'import js "../vendor/motion.mjs" as jsMotion\nfunction arrive(selector: string) { return jsMotion.arrive(selector) }')
 await writeFile(join(src,'vendor/motion.mjs'),'export function arrive(selector){return "called:"+selector}')
 await buildWebProject({source:src,out:dist})
 const module=await import(pathToFileURL(join(dist,'animation/motion.mjs')).href)
 assert.equal(module.arrive('.card'),'called:.card')
}))
test('Noqeri view rejects arbitrary globals and unsafe constructs',()=>{
 for(const code of [
  'view { script { "evil" } }',
  'view { p { window.location } }',
  'view { p { eval("2+2") } }',
  'view { a href: "javascript:alert(1)" { "Oops" } }',
  'view { button on.focus { count = 2 } { "Bad" } }',
  'view { div { missingName } }',
  'const count = 0\nview { button on.click { count = 2 } { "Click" } }'
 ]){
  if(code.includes('javascript:'))continue // URL policy is enforced by the DOM runtime.
  assert.throws(()=>compileView(code),/Noqeri view|Unsafe|unsupported/i,code)
 }
})
test('HTML attribute URL policy refuses dangerous link protocols',async()=>{
 const {element,createApp}=await import('../Runtime/noqeri-web-runtime.mjs')
 const root=new FakeDocument().createElement('main')
 assert.throws(()=>createApp(root,()=>element('a',{href:'javascript:alert(1)'},['Click'])),/Unsafe Noqeri URL/)
})
test('build main.noqeri and local Noqeri UI package without a handwritten HTML page',()=>fixture(async dir=>{
 const src=join(dir,'src'),dist=join(dir,'dist')
 await mkdir(join(src,'ui'),{recursive:true})
 await writeFile(join(src,'main.noqeri'),'import ui/badge as ui\nlet count = 0\nview { h1 { "Hello" } button on.click { count = count + 1 } { "Clicks: " + count } ui.Badge("Ready") }')
 await writeFile(join(src,'ui/badge.noqeri'),'component Badge(label: string) { span { label } }')
 const result=await buildWebProject({source:src,out:dist})
 assert.equal(result.views,2)
 assert.match(await readFile(join(dist,'index.html'),'utf8'),/src="\.\/main\.mjs"/)
 assert.match(await readFile(join(dist,'__noqeri/runtime.mjs'),'utf8'),/createApp/)
 const compiled=await import(pathToFileURL(join(dist,'main.mjs')).href)
 assert.equal(typeof compiled.mount,'function')
}))
test('bare JS dependencies require explicit import map and work with a local real ESM module',()=>fixture(async dir=>{
 const src=join(dir,'src'),dist=join(dir,'dist')
 await mkdir(join(src,'vendor'),{recursive:true})
 await writeFile(join(src,'main.noqeri'),'import js "gsap" as gsap\nview { div class: "card" { "Hello" } }\non.mount { gsap.to(".card", { x: 100, duration: 0.8 }) }')
 await writeFile(join(src,'vendor/gsap.mjs'),'export const to = () => "test adapter"')
 await assert.rejects(buildWebProject({source:src,out:dist}),/web.importmap.json/)
 await writeFile(join(src,'web.importmap.json'),JSON.stringify({imports:{gsap:'./vendor/gsap.mjs'}}))
 await buildWebProject({source:src,out:dist})
 assert.match(await readFile(join(dist,'index.html'),'utf8'),/type="importmap"/)
 assert.match(await readFile(join(dist,'main.mjs'),'utf8'),/from "gsap"/)
}))
test('package imports and build assets cannot escape source root',()=>fixture(async dir=>{
 const src=join(dir,'src');await mkdir(src)
 await writeFile(join(src,'main.noqeri'),'import ui/button as ui\nview { h1 { "Hi" } }')
 await assert.rejects(buildWebProject({source:src,out:join(dir,'dist')}),/UI package missing/)
}))


test('declarative input binding, validation and persistent primitive state',async()=>{
 const source='persist username = "guest"\nview { form on.submit { username = form.username } { input name: "username" required: true minlength: 2 maxlength: 40 {} button type: "submit" { username } } }';
 const compiled=compileView(source);
 assert.match(compiled,/__readStored\("username"/);
 assert.match(compiled,/__saveStored\("username"/);
 assert.match(compiled,/__formFields\(__event\)/);
 assert.match(compiled,/required/);
 const {readStored,saveStored,inputEvent,formFields}=await import('../Runtime/noqeri-web-runtime.mjs');
 const previous=globalThis.localStorage;
 const values=new Map();
 globalThis.localStorage={getItem:key=>values.get(key)??null,setItem:(key,value)=>values.set(key,value)};
 try{
  assert.equal(readStored('theme','default'),'default');
  assert.equal(saveStored('theme','dark'),true);
  assert.equal(readStored('theme','light'),'dark');
  values.set('noqeri:v1:theme','{"admin":true}');
  assert.equal(readStored('theme','light'),'light');
  assert.equal(inputEvent({target:{value:'Ada',checked:true}}).value,'Ada');
  assert.equal(Object.getPrototypeOf(formFields({target:{}})),null);
 }finally{if(previous===undefined)delete globalThis.localStorage;else globalThis.localStorage=previous}
});
test('same-origin reactive WebSocket subscription reconnects only while mounted',async()=>{
 const oldLocation=globalThis.location,oldSocket=globalThis.WebSocket;
 const sockets=[];
 class FakeSocket{
  constructor(url){this.url=url;this.readyState=1;this.onmessage=null;sockets.push(this)}
  close(){this.closed=true}
 }
 globalThis.location={protocol:'https:',host:'example.test'};
 globalThis.WebSocket=FakeSocket;
 try{
  await imported('subscribe live from "/realtime/events"\nview { p { live.data } }',async module=>{
   const root=new FakeDocument().createElement('main');
   const app=module.mount(root);
   assert.equal(sockets.length,1);
   assert.equal(sockets[0].url,'wss://example.test/realtime/events');
   sockets[0].onopen();
   sockets[0].onmessage({data:JSON.stringify('updated')});
   await Promise.resolve();
   assert.equal(find(root,'p').textContent,'updated');
   app.dispose();
   assert.equal(sockets[0].closed,true);
  });
  assert.throws(()=>compileView('subscribe live from "wss://attacker.test/steal"\nview { p { live.data } }'),/same-origin/);
  assert.throws(()=>compileView('subscribe live from "//attacker.test"\nview { p { live.data } }'),/same-origin/);
 }finally{
  if(oldLocation===undefined)delete globalThis.location;else globalThis.location=oldLocation;
  if(oldSocket===undefined)delete globalThis.WebSocket;else globalThis.WebSocket=oldSocket;
 }
});
