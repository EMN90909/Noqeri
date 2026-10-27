import test from 'node:test';
import assert from 'node:assert/strict';
import {createApp, element} from '../Runtime/noqeri-web-runtime.mjs';

class FakeNode {
  constructor(doc,tag=null,text=null) {
    this.ownerDocument=doc; this.tagName=tag?.toUpperCase(); this.nodeType=tag?1:3;
    this.children=[];this.parentNode=null;this.attributes=[];this.attrs=new Map();this.listeners=new Map();
    this.valueText=text??'';
  }
  get childNodes(){return this.children}
  get lastChild(){return this.children.at(-1)}
  get textContent(){return this.nodeType===3?this.valueText:this.children.map(n=>n.textContent).join('')}
  set textContent(v){if(this.nodeType===3)this.valueText=String(v);else this.children=[]}
  getAttribute(k){return this.attrs.get(k)??null}
  setAttribute(k,v){this.attrs.set(k,String(v));this.attributes=[...this.attrs].map(([name,value])=>({name,value}))}
  removeAttribute(k){this.attrs.delete(k);this.attributes=[...this.attrs].map(([name,value])=>({name,value}))}
  appendChild(n){this.children.push(n);n.parentNode=this;return n}
  replaceChild(n,old){const i=this.children.indexOf(old);this.children[i]=n;n.parentNode=this;return old}
  removeChild(n){this.children.splice(this.children.indexOf(n),1)}
  replaceChildren(){this.children=[]}
  addEventListener(k,fn){this.listeners.set(k,fn)}
  removeEventListener(k){this.listeners.delete(k)}
}
function fixture(){
  const doc={createElement(tag){return new FakeNode(doc,tag)},createTextNode(text){return new FakeNode(doc,null,text)}};
  return {doc,root:new FakeNode(doc,'main')};
}
test('hydrates matching server nodes and attaches handlers without replacing DOM',()=>{
  const {doc,root}=fixture(),server=doc.createElement('div'),button=doc.createElement('button');
  button.appendChild(doc.createTextNode('Click'));server.appendChild(button);root.appendChild(server);
  let clicks=0;
  const app=createApp(root,()=>element('div',{},[element('button',{},['Click'],{click:()=>clicks++})]),{hydrate:true});
  assert.equal(root.childNodes[0],server);
  assert.equal(server.childNodes[0],button);
  button.listeners.get('click')({});
  assert.equal(clicks,1);
  app.dispose();
  assert.equal(root.childNodes.length,0);
});
test('hydrates browser-coalesced adjacent text without undefined DOM access',async()=>{
  const {doc,root}=fixture();
  const wrapper=doc.createElement('div'),paragraph=doc.createElement('p');
  paragraph.appendChild(doc.createTextNode('Clicks: 0'));
  wrapper.appendChild(paragraph);root.appendChild(wrapper);
  let count=0;
  const app=createApp(root,()=>element('div',{},[
    element('p',{},['Clicks: ',count])
  ]),{hydrate:true});
  assert.equal(root.childNodes[0],wrapper);
  assert.equal(wrapper.childNodes[0],paragraph);
  assert.equal(paragraph.childNodes.length,1);
  count=1;app.schedule();await Promise.resolve();
  assert.equal(paragraph.textContent,'Clicks: 1');
  assert.equal(paragraph.childNodes.length,1);
  app.dispose();
});
test('recovers safely when an external DOM mutation removed a child',async()=>{
  const {root}=fixture();
  let count=0;
  const app=createApp(root,()=>element('div',{},[
    element('p',{},['Counter ',count])
  ]));
  const wrapper=root.childNodes[0];
  wrapper.replaceChildren();
  count=5;app.schedule();await Promise.resolve();
  assert.equal(wrapper.textContent,'Counter 5');
  app.dispose();
});
test('hydration replaces mismatched tags safely',()=>{
  const {doc,root}=fixture(),wrong=doc.createElement('span');root.appendChild(wrong);
  const app=createApp(root,()=>element('div',{},['Safe']),{hydrate:true});
  assert.notEqual(root.childNodes[0],wrong);
  assert.equal(root.childNodes[0].tagName,'DIV');
  app.dispose();
});
test('subsequent reactive paints retain unchanged elements',async()=>{
  const {root}=fixture();let value=0;
  const app=createApp(root,()=>element('div',{},[element('span',{},[String(value)])]));
  const wrapper=root.childNodes[0],span=wrapper.childNodes[0];
  value=1;app.schedule();app.schedule();await Promise.resolve();
  assert.equal(root.childNodes[0],wrapper);
  assert.equal(wrapper.childNodes[0],span);
  assert.equal(span.textContent,'1');
  app.dispose();
});


test('secure server-side VNode serialization escapes text and forbids unsafe attributes',async()=>{
  const {renderToHtml}=await import('../Runtime/noqeri-web-runtime.mjs');
  const html=renderToHtml(element('div',{class:'safe'},[
    element('a',{href:'/about',target:'_blank'},['<script>&"']),
    element('input',{required:true,value:'a"b'},[])
  ]));
  assert.match(html, /&lt;script&gt;&amp;&quot;/);
  assert.match(html, /rel="noopener noreferrer"/);
  assert.match(html, /<input required value="a&quot;b">/);
  assert.throws(()=>renderToHtml(element('a',{href:'javascript:alert(1)'},['bad'])),/Unsafe Noqeri URL/);
  assert.throws(()=>renderToHtml(element('script',{},['bad'])),/Unsafe Noqeri prerender tag/);
  assert.throws(()=>renderToHtml(element('p',{},['x'.repeat(1024*1024+1)])),/exceeds 1 MiB/);
});
test('zero-HTML Noqeri views prerender real per-route HTML for browser hydration',async()=>{
  const {mkdtemp,mkdir,writeFile,readFile,rm}=await import('node:fs/promises');
  const {tmpdir}=await import('node:os');
  const {join}=await import('node:path');
  const {buildWebProject}=await import('../Runtime/web-project.mjs');
  const dir=await mkdtemp(join(tmpdir(),'noqeri-ssr-route-'));
  try {
    const src=join(dir,'src'),out=join(dir,'dist');
    await mkdir(src);
    await writeFile(join(src,'main.noqeri'),
      'let count = 0\nroute "/" { main { h1 { "Landing" } button on.click { count = count + 1 } { "Clicks: " + count } } }\nroute "/about" { main { h1 { "Details" } } }');
    await buildWebProject({source:src,out});
    const home=await readFile(join(out,'index.html'),'utf8');
    const about=await readFile(join(out,'about/index.html'),'utf8');
    assert.match(home,/<main id="app"><div><main><h1>Landing<\/h1>/);
    assert.match(home,/Clicks: 0/);
    assert.doesNotMatch(home,/<h1>Details<\/h1>/);
    assert.match(about,/<main id="app"><div><main><h1>Details<\/h1>/);
    assert.doesNotMatch(about,/<h1>Landing<\/h1>/);
  } finally { await rm(dir,{recursive:true,force:true}); }
});
test('async Noqeri views remain client-rendered when pre-rendering would be misleading',async()=>{
  const {mkdtemp,mkdir,writeFile,readFile,rm}=await import('node:fs/promises');
  const {tmpdir}=await import('node:os');
  const {join}=await import('node:path');
  const {buildWebProject}=await import('../Runtime/web-project.mjs');
  const dir=await mkdtemp(join(tmpdir(),'noqeri-async-view-'));
  try {
    const src=join(dir,'src'),out=join(dir,'dist');
    await mkdir(src);
    await writeFile(join(src,'main.noqeri'),'load data from "/api/data"\nview { p { data.loading } }');
    await buildWebProject({source:src,out});
    assert.match(await readFile(join(out,'index.html'),'utf8'),/<main id="app"><\/main>/);
  } finally { await rm(dir,{recursive:true,force:true}); }
});
