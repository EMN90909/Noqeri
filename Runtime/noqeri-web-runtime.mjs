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
  if(!Array.isArray(children))throw TypeError('Noqeri element children must be an array');
  // The HTML parser coalesces adjacent text nodes. Normalize the VNode tree
  // identically for SSR, hydration and future updates, including text + state.
  const normalized=[];
  for(const child of children){
    if(isNode(child)){
      normalized.push(child);
    } else if(Array.isArray(child)) {
      throw TypeError('Noqeri nested child arrays are unsupported');
    } else {
      const text=child==null?'':String(child);
      if(normalized.length && typeof normalized[normalized.length-1]==='string')
        normalized[normalized.length-1]+=text;
      else normalized.push(text);
    }
  }
  return {__noqeri:1,tag,attrs,children:normalized,events};
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
  const same=Boolean(dom) && (old?((old.text!==undefined&&now.text!==undefined)||(old.tag&&old.tag===now.tag)):(hydrating&&compatible));
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


// Noqeri Motion: an independent Web Animations API adapter. This is NOT GSAP.
// No arbitrary CSS/JS values are evaluated. Text and form state remain owned
// by the Noqeri DOM reconciler, not by the motion layer.
const MOTION_EASE = Object.freeze({
  linear:'linear',in:'cubic-bezier(.42,0,1,1)',
  out:'cubic-bezier(0,0,.58,1)',inOut:'cubic-bezier(.42,0,.58,1)',
  back:'cubic-bezier(.34,1.56,.64,1)',smooth:'cubic-bezier(.22,1,.36,1)'
});
const motionNumber=(value,min,max,key)=>{
  if(!Number.isInteger(value)||value<min||value>max)
    throw Error('Noqeri motion invalid '+key);
  return value;
};
export function createMotion(root,definitions=[],sequences=[]) {
  if(!root||typeof root.querySelectorAll!=='function')
    throw TypeError('Noqeri motion requires a DOM root');
  if(!Array.isArray(definitions)||definitions.length>128||
     !Array.isArray(sequences)||sequences.length>32)
    throw Error('Noqeri motion declaration limit exceeded');
  const specs=new Map(),timelineNames=new Set(),running=new Map(),watched=new Map();
  const observers=new Set(),scrollWatches=new Map();
  let mutationObserver=null,disposed=false,scheduled=false;
  const reduced=()=>typeof matchMedia==='function'&&matchMedia('(prefers-reduced-motion: reduce)').matches;
  for(const item of definitions){
    if(!item||typeof item!=='object'||typeof item.name!=='string'||
       !/^[A-Za-z_][A-Za-z0-9_]{0,63}$/.test(item.name)||specs.has(item.name))
      throw Error('Noqeri motion duplicate or invalid name');
    const raw=item;
    const valid=['name','target','trigger','ease','fromOpacity','toOpacity',
      'fromX','toX','fromY','toY','fromScale','toScale','fromRotation','toRotation',
      'duration','delay','offset','stagger','repeat','yoyo','once'];
    if(Object.keys(raw).some(k=>!valid.includes(k)))
      throw Error('Noqeri motion unsupported property');
    if(typeof raw.target!=='string'||!/^([.#])[A-Za-z_][A-Za-z0-9_-]{0,63}$/.test(raw.target))
      throw Error('Noqeri motion requires safe class/id target');
    const spec={
      name:raw.name,target:raw.target,trigger:raw.trigger??'mount',ease:raw.ease??'smooth',
      fromOpacity:motionNumber(raw.fromOpacity??100,0,100,'fromOpacity'),
      toOpacity:motionNumber(raw.toOpacity??100,0,100,'toOpacity'),
      fromX:motionNumber(raw.fromX??0,-2000,2000,'fromX'),
      toX:motionNumber(raw.toX??0,-2000,2000,'toX'),
      fromY:motionNumber(raw.fromY??0,-2000,2000,'fromY'),
      toY:motionNumber(raw.toY??0,-2000,2000,'toY'),
      fromScale:motionNumber(raw.fromScale??100,1,400,'fromScale'),
      toScale:motionNumber(raw.toScale??100,1,400,'toScale'),
      fromRotation:motionNumber(raw.fromRotation??0,-1080,1080,'fromRotation'),
      toRotation:motionNumber(raw.toRotation??0,-1080,1080,'toRotation'),
      duration:motionNumber(raw.duration??500,0,30000,'duration'),
      delay:motionNumber(raw.delay??0,0,30000,'delay'),
      offset:motionNumber(raw.offset??0,-30000,30000,'offset'),
      stagger:motionNumber(raw.stagger??0,0,10000,'stagger'),
      repeat:motionNumber(raw.repeat??0,0,10,'repeat'),
      yoyo:raw.yoyo??false,once:raw.once??true
    };
    if(!['mount','scroll','timeline'].includes(spec.trigger)||!Object.hasOwn(MOTION_EASE,spec.ease)||
       typeof spec.yoyo!=='boolean'||typeof spec.once!=='boolean'||
       (spec.offset!==0&&spec.trigger!=='timeline'))
      throw Error('Noqeri motion invalid trigger/easing/flag');
    specs.set(spec.name,spec);watched.set(spec.name,new WeakSet());
  }
  const allocatedTimelineSteps=new Set();
  for(const seq of sequences){
    if(!seq||typeof seq.name!=='string'||!Array.isArray(seq.steps)||
       !/^[A-Za-z_][A-Za-z0-9_]{0,63}$/.test(seq.name)||
       timelineNames.has(seq.name)||specs.has(seq.name)||
       seq.steps.length<1||seq.steps.length>32)
      throw Error('Noqeri motion invalid timeline');
    timelineNames.add(seq.name);
    for(const name of seq.steps){
      if(!specs.has(name)||specs.get(name).trigger!=='timeline'||
         allocatedTimelineSteps.has(name))
        throw Error('Noqeri timeline step must be unique and use trigger timeline: '+name);
      allocatedTimelineSteps.add(name);
    }
  }
  for(const spec of specs.values())
    if(spec.trigger==='timeline'&&!allocatedTimelineSteps.has(spec.name))
      throw Error('Noqeri motion timeline step is unassigned: '+spec.name);
  const keys=s=>({
    opacity:s.fromOpacity/100,
    transform:'translate('+s.fromX+'px,'+s.fromY+'px) rotate('+s.fromRotation+'deg) scale('+(s.fromScale/100)+')'
  });
  const ends=s=>({
    opacity:s.toOpacity/100,
    transform:'translate('+s.toX+'px,'+s.toY+'px) rotate('+s.toRotation+'deg) scale('+(s.toScale/100)+')'
  });
  const elements=s=>Array.from(root.querySelectorAll(s.target)).slice(0,256);
  const animate=(spec,node,delay=0)=>{
    if(disposed||watched.get(spec.name).has(node))return null;
    watched.get(spec.name).add(node);
    // When motion is reduced, the static/no-script final appearance is kept.
    if(reduced()||typeof node.animate!=='function')return null;
    const animation=node.animate([keys(spec),ends(spec)],{
      duration:spec.duration,delay,fill:'both',easing:MOTION_EASE[spec.ease],
      iterations:spec.repeat+1,direction:spec.yoyo?'alternate':'normal'
    });
    if(!running.has(spec.name))running.set(spec.name,new Set());
    running.get(spec.name).add(animation);
    return animation;
  };
  const run=(spec,offset=0)=>{
    const matches=elements(spec);
    matches.forEach((node,index)=>animate(spec,node,offset+spec.delay+index*spec.stagger));
    return spec.delay+spec.duration*(spec.repeat+1)+
      spec.stagger*Math.max(0,matches.length-1);
  };
  const sequence=(seq)=>{
    // Negative step offsets allow overlaps, while the timeline end never rewinds.
    let cursor=0;
    for(const name of seq.steps){
      const spec=specs.get(name),start=Math.max(0,cursor+spec.offset);
      cursor=Math.max(cursor,start+run(spec,start));
    }
  };
  const scroll=(spec)=>{
    let registrations=scrollWatches.get(spec.name);
    if(!registrations){registrations=new Map();scrollWatches.set(spec.name,registrations);}
    const marked=watched.get(spec.name),nodes=elements(spec);
    for(const element of nodes){
      if(marked.has(element)||registrations.has(element))continue;
      if(typeof IntersectionObserver!=='function'){
        animate(spec,element,spec.delay+nodes.indexOf(element)*spec.stagger);
        continue;
      }
      let entered=false,active=null;
      const observer=new IntersectionObserver(entries=>{
        for(const entry of entries){
          if(disposed||entry.target!==element)continue;
          if(entry.isIntersecting){
            if(entered)continue;
            entered=true;
            const index=elements(spec).indexOf(element);
            active=animate(spec,element,spec.delay+Math.max(0,index)*spec.stagger);
            if(spec.once){observer.disconnect();observers.delete(observer);registrations.delete(element);}
          }else if(!spec.once&&entered){
            entered=false;
            active?.cancel();running.get(spec.name)?.delete(active);
            active=null;
            marked.delete(element); // permit re-entry, not a new observer
          }
        }
      },{threshold:0.12});
      registrations.set(element,{observer,get active(){return active}});
      observers.add(observer);observer.observe(element);
    }
  };
  const refresh=()=>{
    if(disposed)return;
    if(typeof root.contains==='function')
      for(const [name,registrations] of scrollWatches)
        for(const [node,record] of registrations)
          if(!root.contains(node)){
            record.observer.disconnect();observers.delete(record.observer);
            record.active?.cancel();running.get(name)?.delete(record.active);
            watched.get(name).delete(node);registrations.delete(node);
          }
    for(const spec of specs.values()){
      if(spec.trigger==='mount')run(spec);
      if(spec.trigger==='scroll')scroll(spec);
    }
    for(const seq of sequences)sequence(seq);
  };
  refresh();
  if(typeof MutationObserver==='function'){
    mutationObserver=new MutationObserver(()=>{
      if(disposed||scheduled)return;
      scheduled=true;
      queueMicrotask(()=>{scheduled=false;refresh();});
    });
    mutationObserver.observe(root,{childList:true,subtree:true});
  }
  return {
    refresh,
    play(name){
      if(disposed)return false;
      const seq=sequences.find(x=>x.name===name);
      if(seq)sequence(seq);
      else if(specs.has(name))run(specs.get(name));
      else return false;
      for(const key of (seq?.steps??[name]))
        for(const animation of running.get(key)??[])animation.play();
      return true;
    },
    pause(name){
      if(disposed||(!specs.has(name)&&!timelineNames.has(name)))return false;
      const seq=sequences.find(x=>x.name===name);
      for(const key of (seq?.steps??[name]))
        for(const animation of running.get(key)??[])animation.pause();
      return true;
    },
    reverse(name){
      if(disposed||(!specs.has(name)&&!timelineNames.has(name)))return false;
      const seq=sequences.find(x=>x.name===name);
      for(const key of (seq?.steps??[name]))
        for(const animation of running.get(key)??[])animation.reverse();
      return true;
    },
    restart(name){
      if(disposed||(!specs.has(name)&&!timelineNames.has(name)))return false;
      const seq=sequences.find(x=>x.name===name);
      for(const key of (seq?.steps??[name]))
        for(const animation of running.get(key)??[]){animation.currentTime=0;animation.play();}
      return true;
    },
    dispose(){
      if(disposed)return;
      disposed=true;
      mutationObserver?.disconnect();
      for(const observer of observers)observer.disconnect();
      observers.clear();scrollWatches.clear();
      for(const animations of running.values())
        for(const animation of animations)animation.cancel();
      running.clear();
    }
  };
}
