// SPDX-License-Identifier: Apache-2.0
// Noqeri browser runtime: tiny keyed-by-position DOM reconciler, no framework.
// Text always uses textContent. No innerHTML, eval, or dynamic source execution.
const safeKey = k => /^[a-z][a-z0-9-]*$/.test(k) && !/^on/i.test(k) && !['constructor','prototype','__proto__','style','srcdoc','innerhtml'].includes(k);
const allowed = k => new Set('id class title role href src alt type name value placeholder for disabled checked target rel tabindex required minlength maxlength min max autocomplete inputmode readonly'.split(' ')).has(k) || /^aria-[a-z0-9-]+$/.test(k) || /^data-[a-z0-9-]+$/.test(k);
const allowedEvent = new Set(['click','change','input','submit']);
export function safeMember(object,key) {
  if (object===null||object===undefined)return '';
  if (!/^[a-zA-Z_][a-zA-Z_0-9]*$/.test(key)||['constructor','prototype','__proto__'].includes(key))throw Error('Unsafe Noqeri member '+key);
  return Object.hasOwn(Object(object),key)?object[key]:'';
}
export function element(tag,attrs={},children=[],events={}) {
  if(!/^[a-z][a-z0-9]*$/.test(tag))throw Error('Unsafe Noqeri element tag');
  return {__noqeri:1,tag,attrs,children,events};
}
const isNode=value=>Boolean(value&&typeof value==='object'&&value.__noqeri===1);
const normalize=v=>isNode(v)?v:{__noqeri:1,text: v===null||v===undefined?'':String(v)};
function attribute(node,name,value) {
  if(!safeKey(name)||!allowed(name))throw Error('Unsafe Noqeri attribute '+name);
  if(name==='href'||name==='src'){
    const raw=String(value).trim();
    if(!/^(?:https:\/\/|\/(?!\/)|\.\.?\/|#|mailto:|tel:)/i.test(raw))throw Error('Unsafe Noqeri URL attribute');
  }
  if(name==='disabled'||name==='checked'||name==='required'||name==='readonly'){if(value)node.setAttribute(name,'');else node.removeAttribute(name);return}
  if(value===null||value===undefined||value===false){node.removeAttribute(name);return}
  node.setAttribute(name,String(value));
}
function syncEvents(node,previous,next){
  if(!node.__noqeriEvents)node.__noqeriEvents=Object.create(null);
  const listeners=node.__noqeriEvents;
  for(const kind of Object.keys(listeners))if(!Object.hasOwn(next,kind)){
    node.removeEventListener(kind,listeners[kind].dispatch);delete listeners[kind];
  }
  for(const [kind,handler] of Object.entries(next)){
    if(!allowedEvent.has(kind)||typeof handler!=='function')throw Error('Unsupported Noqeri event '+kind);
    if(listeners[kind])listeners[kind].handler=handler;
    else {
      const record={handler,dispatch:null};
      record.dispatch=e=>record.handler(e);
      listeners[kind]=record;
      node.addEventListener(kind,record.dispatch);
    }
  }
}
function patch(parent,oldVNode,newVNode,index){
  const doc=parent.ownerDocument;
  const old=oldVNode?normalize(oldVNode):null,now=normalize(newVNode);
  let dom=parent.childNodes[index];
  const same=old&&((old.text!==undefined&&now.text!==undefined)||(old.tag&&old.tag===now.tag));
  if(!same){
    const made=now.text!==undefined?doc.createTextNode(now.text):doc.createElement(now.tag);
    if(dom)parent.replaceChild(made,dom);else parent.appendChild(made);
    dom=made;
  }
  if(now.text!==undefined) {
    if(dom.textContent!==now.text)dom.textContent=now.text;
    return;
  }
  for(const name of Object.keys(old?.attrs||{}))if(!Object.hasOwn(now.attrs,name))dom.removeAttribute(name);
  for(const [name,value] of Object.entries(now.attrs))attribute(dom,name,value);
  if(now.attrs.target==='_blank'&&!now.attrs.rel)dom.setAttribute('rel','noopener noreferrer');
  syncEvents(dom,old?.events||{},now.events);
  for(let i=0;i<now.children.length;i++)patch(dom,old?.children?.[i],now.children[i],i);
  while(dom.childNodes.length>now.children.length)dom.removeChild(dom.lastChild);
}
export function createApp(root,render) {
  if(!root||!root.ownerDocument||typeof render!=='function')throw Error('Noqeri browser root/document unavailable');
  let current=null,queued=false,disposed=false;
  const controllers=new Set();
  const paint=()=>{
    if(disposed)return;
    const next=render();
    patch(root,current,next,0);
    while(root.childNodes.length>1)root.removeChild(root.lastChild);
    current=next;
  };
  const schedule=()=>{
    if(disposed||queued)return;
    queued=true;queueMicrotask(()=>{queued=false;paint()});
  };
  paint();
  return {
    schedule,
    async load(url,onSuccess,onError){
      if(typeof url!=='string'||!(/^(?:\/(?!\/)|\.(?:\/|\.\/))/.test(url)))throw Error('Noqeri data requests must use same-origin paths');
      const controller=new AbortController();controllers.add(controller);
      const timer=setTimeout(()=>controller.abort(),10000);
      try{
        const response=await fetch(url,{credentials:'same-origin',signal:controller.signal,redirect:'error',headers:{Accept:'application/json'}});
        if(!response.ok)throw Error('Request returned HTTP '+response.status);
        const body=await response.text();
        if(body.length>1024*1024)throw Error('Response too large');
        const data=JSON.parse(body);
        if(!disposed)onSuccess(data);
      }catch(error){if(!disposed)onError(error)}
      finally{clearTimeout(timer);controllers.delete(controller)}
    },
    dispose(){
      if(disposed)return;
      disposed=true;
      for(const controller of controllers)controller.abort();
      controllers.clear();
      // Removing the root's DOM tree removes every registered event handler.
      root.replaceChildren();current=null;
    }
  };
}


/**
 * Noqeri router capability. Page authors write only Noqeri route declarations;
 * the browser adapter owns history, navigation and cleanup.
 * Only local, static absolute paths are accepted (no external redirects).
 */
export function createRouter(root,onChange) {
  if(!root||typeof onChange!=='function')throw TypeError('Noqeri router requires a root and callback');
  const win=typeof window==='undefined'?null:window;
  const valid=path=>typeof path==='string'&&/^\/(?:[A-Za-z0-9_-]+(?:\/[A-Za-z0-9_-]+)*)?$/.test(path);
  const current=()=>{
    const raw=win?.location?.pathname??'/';
    const path=raw.replace(/\/+$/,'')||'/';
    return valid(path)?path:'';
  };
  const navigate=path=>{
    if(!valid(path)||!win?.history?.pushState)return false;
    if(path!==current())win.history.pushState(null,'',path);
    onChange();
    return true;
  };
  const pop=()=>onChange();
  const click=event=>{
    if(event.defaultPrevented||event.button>0||event.ctrlKey||event.metaKey||event.altKey||event.shiftKey)return;
    const anchor=event.target?.closest?.('a[href]');
    if(!anchor||anchor.hasAttribute('download')||!root.contains?.(anchor))return;
    const target=anchor.getAttribute('target');
    if(target&&target!=='_self')return;
    const href=anchor.getAttribute('href');
    if(!valid(href))return;
    event.preventDefault();
    navigate(href);
  };
  win?.addEventListener?.('popstate',pop);
  root.addEventListener?.('click',click);
  let disposed=false;
  return {
    path:current,
    navigate:path=>!disposed&&navigate(path),
    dispose(){
      if(disposed)return;
      disposed=true;
      win?.removeEventListener?.('popstate',pop);
      root.removeEventListener?.('click',click);
    }
  };
}

/** A small, read-only event value. Never expose the browser DOM through .noqeri code. */
export function eventInfo(nativeEvent){
  const target=nativeEvent?.target;
  const type=String(nativeEvent?.type||'');
  if(type==='submit')nativeEvent?.preventDefault?.();
  const form=Object.create(null);
  if(type==='submit' && target && typeof FormData!=='undefined'){
    let count=0;
    for(const [name,value] of new FormData(target)){
      if(++count>64)break;
      if(/^[A-Za-z_][A-Za-z_0-9]{0,63}$/.test(name) && typeof value==='string' && value.length<=8192 && !Object.hasOwn(form,name))form[name]=value;
    }
  }
  return Object.freeze({type,value:typeof target?.value==='string'?target.value:'',checked:target?.checked===true,valid:target?.validity?.valid!==false,form:Object.freeze(form)});
}
