// SPDX-License-Identifier: Apache-2.0
// Noqeri browser runtime: tiny keyed-by-position DOM reconciler, no framework.
// Text always uses textContent. No innerHTML, eval, or dynamic source execution.
const safeKey = k => /^[a-z][a-z0-9-]*$/.test(k) && !/^on/i.test(k) && !['constructor','prototype','__proto__','style','srcdoc','innerhtml'].includes(k);
const ATTRIBUTES = new Set('id class title role href src alt type name value placeholder for disabled checked required minlength maxlength min max step autocomplete inputmode target rel tabindex'.split(' '));
const allowed = k => ATTRIBUTES.has(k) || /^aria-[a-z0-9-]+$/.test(k) || /^data-[a-z0-9-]+$/.test(k);
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
// Trusted server-side serialization of a Noqeri VNode for static pre-rendering.
// This does not execute event callbacks, user HTML strings, or JavaScript.
const SSR_TAGS = new Set('a abbr article aside b blockquote br button caption code dd details div dl dt em fieldset figcaption figure footer form h1 h2 h3 h4 h5 h6 header hr i img input label li main nav ol option p picture pre section select small span strong summary table tbody td textarea th thead time tr ul'.split(' '));
const SSR_VOID = new Set(['br','hr','img','input']);
const escapeHtml = value => String(value).replace(/[&<>"']/g, ch => ({
  '&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'
})[ch]);
export function renderToHtml(vnode) {
  let size = 0;
  function emit(value) {
    size += value.length;
    if (size > 1024*1024) throw Error('Noqeri prerender exceeds 1 MiB');
    return value;
  }
  function visit(value, depth = 0) {
    if (depth > 64) throw Error('Noqeri prerender nesting limit exceeded');
    if (Array.isArray(value)) return value.map(child => visit(child,depth+1)).join('');
    const node = normalize(value);
    if (node.text !== undefined) return emit(escapeHtml(node.text));
    if (!SSR_TAGS.has(node.tag)) throw Error('Unsafe Noqeri prerender tag '+node.tag);
    let attrs = '';
    for (const [key,val] of Object.entries(node.attrs||{})) {
      if (!safeKey(key) || !allowed(key)) throw Error('Unsafe Noqeri attribute '+key);
      if (val == null || val === false) continue;
      if (key === 'href' || key === 'src') {
        const raw = String(val).trim();
        if (!/^(?:https:\/\/|\/(?!\/)|\.\.?\/|#|mailto:|tel:)/i.test(raw))
          throw Error('Unsafe Noqeri URL attribute');
      }
      if (['disabled','checked','required'].includes(key)) {
        if (val) attrs += ' '+key;
      } else attrs += ' '+key+'="'+escapeHtml(val)+'"';
    }
    if (node.attrs?.target === '_blank' && !node.attrs.rel)
      attrs += ' rel="noopener noreferrer"';
    if (SSR_VOID.has(node.tag)) {
      if (node.children?.length) throw Error('Noqeri void tag cannot have children');
      return emit('<'+node.tag+attrs+'>');
    }
    const children = (node.children||[]).map(child=>visit(child,depth+1)).join('');
    return emit('<'+node.tag+attrs+'>'+children+'</'+node.tag+'>');
  }
  return visit(vnode);
}

const normalize=v=>isNode(v)?v:{__noqeri:1,text: v===null||v===undefined?'':String(v)};
function attribute(node,name,value) {
  if(!safeKey(name)||!allowed(name))throw Error('Unsafe Noqeri attribute '+name);
  if(name==='href'||name==='src'){
    const raw=String(value).trim();
    if(!/^(?:https:\/\/|\/(?!\/)|\.\.?\/|#|mailto:|tel:)/i.test(raw))throw Error('Unsafe Noqeri URL attribute');
  }
  if(name==='disabled'||name==='checked'||name==='required'){if(value)node.setAttribute(name,'');else node.removeAttribute(name);return}
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
      record.dispatch=e=>{
        if(kind==='submit'){
          if(e?.target?.reportValidity && !e.target.reportValidity())return;
          e?.preventDefault?.();
        }
        record.handler(e);
      };
      listeners[kind]=record;
      node.addEventListener(kind,record.dispatch);
    }
  }
}
function patch(parent,oldVNode,newVNode,index,hydrating=false){
  const doc=parent.ownerDocument;
  const old=oldVNode?normalize(oldVNode):null,now=normalize(newVNode);
  let dom=parent.childNodes[index];
  // Adopt matching server-rendered nodes on the first pass instead of replacing them.
  const compatible=dom&&((now.text!==undefined&&dom.nodeType===3)||(now.tag&&dom.nodeType===1&&dom.tagName?.toLowerCase()===now.tag));
  const same=old?((old.text!==undefined&&now.text!==undefined)||(old.tag&&old.tag===now.tag)):(hydrating&&compatible);
  if(!same){
    const made=now.text!==undefined?doc.createTextNode(now.text):doc.createElement(now.tag);
    if(dom)parent.replaceChild(made,dom);else parent.appendChild(made);
    dom=made;
  }
  if(now.text!==undefined) {
    if(dom.textContent!==now.text)dom.textContent=now.text;
    return;
  }
  if(hydrating&&!old){
    // Remove stale SSR attributes, but never touch browser-owned input properties.
    for(const item of Array.from(dom.attributes||[]))if(!Object.hasOwn(now.attrs,item.name))dom.removeAttribute(item.name);
  }else for(const name of Object.keys(old?.attrs||{}))if(!Object.hasOwn(now.attrs,name))dom.removeAttribute(name);
  for(const [name,value] of Object.entries(now.attrs))if(!old||old.attrs?.[name]!==value)attribute(dom,name,value);
  if(now.attrs.target==='_blank'&&!now.attrs.rel)dom.setAttribute('rel','noopener noreferrer');
  syncEvents(dom,old?.events||{},now.events);
  for(let i=0;i<now.children.length;i++)patch(dom,old?.children?.[i],now.children[i],i,hydrating&&!old);
  while(dom.childNodes.length>now.children.length)dom.removeChild(dom.lastChild);
}
// Explicit browser persistence is for nonsecret primitive UI preferences only.
export function readStored(name,fallback) {
  if(!/^[A-Za-z_][A-Za-z_0-9]{0,63}$/.test(name))throw Error('Invalid Noqeri persisted state name');
  if(!['string','number','boolean'].includes(typeof fallback))throw Error('Noqeri persisted state requires primitive');
  try{
    const raw=globalThis.localStorage?.getItem('noqeri:v1:'+name);
    if(!raw||raw.length>4096)return fallback;
    const value=JSON.parse(raw);
    return typeof value===typeof fallback && (typeof value!=='number'||Number.isFinite(value))?value:fallback;
  }catch{return fallback}
}
export function saveStored(name,value) {
  if(!/^[A-Za-z_][A-Za-z_0-9]{0,63}$/.test(name))throw Error('Invalid Noqeri persisted state name');
  if(!['string','number','boolean'].includes(typeof value)||
     (typeof value==='number'&&!Number.isFinite(value)))throw Error('Noqeri persisted state requires primitive');
  const raw=JSON.stringify(value);
  if(raw.length>4096)throw Error('Noqeri persisted state exceeds limit');
  try{globalThis.localStorage?.setItem('noqeri:v1:'+name,raw);return true}catch{return false}
}
// Event data is a bounded, own-property-only object, never a raw DOM object.
export function inputEvent(event) {
  return {value:String(event?.target?.value??'').slice(0,4096),checked:!!event?.target?.checked};
}
export function formFields(event) {
  const values=Object.create(null);
  const form=event?.target;
  if(!form||typeof form.tagName!=='string'||form.tagName.toLowerCase()!=='form'||typeof FormData==='undefined')return values;
  let count=0;
  for(const [name,value] of new FormData(form)){
    if(++count>32)throw Error('Noqeri form has too many fields');
    if(!/^[A-Za-z_][A-Za-z_0-9]{0,63}$/.test(name)||name.startsWith('_nq_'))continue;
    if(typeof value!=='string'||value.length>4096)throw Error('Noqeri field too large or unsupported');
    values[name]=value;
  }
  return values;
}
export function createApp(root,render,{hydrate=false}={}) {
  if(!root||!root.ownerDocument||typeof render!=='function')throw Error('Noqeri browser root/document unavailable');
  let current=null,queued=false,disposed=false;
  const controllers=new Set(),sockets=new Set(),reconnects=new Set();
  const paint=()=>{
    if(disposed)return;
    const next=render();
    patch(root,current,next,0,hydrate&&current===null);
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
    socket(path,onUpdate,onError){
      if(typeof path!=='string'||!/^\/(?!\/)[A-Za-z0-9/_-]{1,256}$/.test(path)||
         path.includes('..')||path.includes('//'))
        throw Error('Noqeri WebSocket path must be a fixed same-origin path');
      if(typeof onUpdate!=='function'||typeof onError!=='function')
        throw Error('Noqeri WebSocket callbacks are required');
      if(typeof WebSocket==='undefined'||typeof location==='undefined')
        throw Error('WebSocket browser transport is unavailable');
      let closed=false,attempt=0,active=null;
      const endpoint=(location.protocol==='https:'?'wss://':'ws://')+location.host+path;
      const connection={close(){closed=true;if(active){active.onclose=null;active.close();}sockets.delete(connection)}};
      sockets.add(connection);
      const connect=()=>{
        if(closed||disposed)return;
        try{
          const ws=new WebSocket(endpoint);
          active=ws;
          ws.onopen=()=>{attempt=0;onUpdate({connected:true})};
          ws.onmessage=event=>{
            try{
              if(typeof event.data!=='string'||event.data.length>65536)
                throw Error('Realtime JSON frame exceeds 64 KiB');
              const data=JSON.parse(event.data);
              if(!closed&&!disposed)onUpdate({connected:true,data});
            }catch(e){if(!closed&&!disposed)onError(e)}
          };
          ws.onerror=()=>{if(!closed&&!disposed)onError(Error('WebSocket connection failed'))};
          ws.onclose=()=>{
            if(closed||disposed)return;
            onUpdate({connected:false});
            const delay=Math.min(15000,500*Math.pow(2,Math.min(attempt++,5)));
            const timer=setTimeout(()=>{reconnects.delete(timer);connect()},delay);
            reconnects.add(timer);
          };
        }catch(e){if(!closed&&!disposed)onError(e)}
      };
      connect();
      return connection;
    },
    dispose(){
      if(disposed)return;
      disposed=true;
      for(const controller of controllers)controller.abort();
      controllers.clear();
      for(const socket of [...sockets])socket.close();
      for(const timer of reconnects)clearTimeout(timer);
      reconnects.clear();
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
