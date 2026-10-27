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
