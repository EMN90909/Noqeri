// SPDX-License-Identifier: Apache-2.0
// Built-in browser utilities. No tokens are stored in localStorage or cookies by JavaScript.
// A Noqeri application imports this as: import "noqeri/app" as app.
const own=(value,key)=>Object.hasOwn(Object(value),key);
const field=/^[A-Za-z_][A-Za-z0-9_]{0,63}$/;
const sameOrigin=path=>typeof path==='string' && /^\/(?!\/)[A-Za-z0-9_./?=&%-]*$/.test(path) &&
  !path.split(/[?#]/,1)[0].split('/').includes('..') && !/%(?:2f|5c|2e|00)/i.test(path);
function route(path){if(!sameOrigin(path))throw Error('Noqeri app: use a safe same-origin /path');return path}
function plain(obj){return obj!==null&&typeof obj==='object'&&!Array.isArray(obj)&&Object.getPrototypeOf(obj)===Object.prototype}
function notify(listeners,value){for(const fn of [...listeners])fn(value)}
export function signal(initial){
  let value=initial;const listeners=new Set();
  return Object.freeze({
    get(){return value},
    set(next){value=typeof next==='function'?next(value):next;notify(listeners,value);return value},
    subscribe(fn){if(typeof fn!=='function')throw TypeError('subscriber must be a function');listeners.add(fn);return ()=>listeners.delete(fn)}
  });
}
export function validate(values,rules){
  if(!plain(values)||!plain(rules))throw TypeError('Noqeri forms require plain objects');
  const errors=Object.create(null);
  for(const [name,rule] of Object.entries(rules)){
    if(!field.test(name)||!plain(rule))throw Error('Noqeri form: invalid field or rule');
    const value=own(values,name)?values[name]:'';
    if(value!==null&&typeof value!=='string'&&typeof value!=='boolean'&&typeof value!=='number'){
      errors[name]='Enter a valid value';continue;
    }
    const s=value===null?'':String(value);
    if(rule.required && (!s.trim()||value===false)){errors[name]='This field is required';continue}
    if(s && Number.isInteger(rule.minLength)&&s.length<rule.minLength){errors[name]='Use at least '+rule.minLength+' characters';continue}
    if(s && Number.isInteger(rule.maxLength)&&s.length>rule.maxLength){errors[name]='Use at most '+rule.maxLength+' characters';continue}
    if(s && rule.email && !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(s)){errors[name]='Enter a valid email address';continue}
    if(s && rule.numeric && !Number.isFinite(Number(s))){errors[name]='Enter a number';continue}
    if(s && typeof rule.min==='number' && Number(s)<rule.min){errors[name]='Value is too small';continue}
    if(s && typeof rule.max==='number' && Number(s)>rule.max){errors[name]='Value is too large'}
  }
  return {ok:Object.keys(errors).length===0,errors};
}
export async function request(path,{method='GET',body,csrf='',signal:abortSignal,fetchImpl=globalThis.fetch}={}){
  route(path);
  method=String(method).toUpperCase();
  if(!['GET','POST','PUT','PATCH','DELETE'].includes(method))throw Error('Noqeri app: unsupported HTTP method');
  if(method==='GET'&&body!==undefined)throw Error('Noqeri app: GET cannot include body');
  if(typeof fetchImpl!=='function')throw TypeError('Fetch is unavailable');
  const headers={Accept:'application/json'};
  if(method!=='GET'){
    if(body!==undefined){headers['Content-Type']='application/json'}
    if(csrf)headers['X-CSRF-Token']=csrf;
  }
  const response=await fetchImpl(path,{method,headers,body:body===undefined?undefined:JSON.stringify(body),credentials:'same-origin',redirect:'error',signal:abortSignal});
  const raw=await response.text();
  if(raw.length>1024*1024)throw Error('Noqeri app: response exceeds 1 MiB');
  let data=null;
  try{data=raw?JSON.parse(raw):null}catch{throw Error('Noqeri app: server returned invalid JSON')}
  if(!response.ok){const error=new Error('Noqeri request failed (HTTP '+response.status+')');error.status=response.status;throw error}
  return data;
}
export function resource(path,{fetchImpl=globalThis.fetch}={}){
  route(path);const state=signal({loading:false,data:null,error:null});
  let controller=null,disposed=false,serial=0;
  const refresh=async()=>{
    if(disposed)throw Error('Noqeri resource disposed');
    controller?.abort();controller=new AbortController();const current=++serial;
    state.set({...state.get(),loading:true,error:null});
    try{const data=await request(path,{fetchImpl,signal:controller.signal});
      if(!disposed&&current===serial)state.set({loading:false,data,error:null});
      return data;
    }catch(error){if(!disposed&&current===serial)state.set({loading:false,data:null,error:'Request failed'});throw error}
  };
  return {get:state.get,subscribe:state.subscribe,refresh,dispose(){disposed=true;controller?.abort()}};
}
export function session(path='/api/session',{fetchImpl=globalThis.fetch}={}){
  route(path);const state=signal({loading:false,user:null,csrf:null});
  return {
    get:state.get,subscribe:state.subscribe,
    async refresh(){state.set({...state.get(),loading:true});try{const data=await request(path,{fetchImpl});state.set({loading:false,user:data?.user??null,csrf:data?.csrf??null});return state.get()}catch(error){state.set({loading:false,user:null,csrf:null});throw error}},
    async logout(){const csrf=state.get().csrf;if(!csrf)throw Error('Noqeri auth: no active session');
      await request(path,{method:'DELETE',csrf,fetchImpl});state.set({loading:false,user:null,csrf:null})}
  };
}
export function socket(path,{onMessage=()=>{},onState=()=>{},onError=()=>{},reconnect=true,maxQueue=32,
  WebSocketImpl=globalThis.WebSocket,locationImpl=globalThis.location}={}){
  route(path);
  if(typeof WebSocketImpl!=='function'||!locationImpl||!/^https?:$/.test(locationImpl.protocol))
    throw Error('Noqeri WebSocket requires a browser HTTP(S) origin');
  if(!Number.isInteger(maxQueue)||maxQueue<0||maxQueue>256)throw RangeError('Invalid WebSocket queue capacity');
  const url=(locationImpl.protocol==='https:'?'wss://':'ws://')+locationImpl.host+path;
  const pending=[];let ws=null,closed=false,timer=null,attempt=0;
  function start(){
    if(closed)return;
    ws=new WebSocketImpl(url);onState('connecting');
    ws.onopen=()=>{attempt=0;onState('open');while(pending.length&&ws.readyState===1)ws.send(pending.shift())};
    ws.onmessage=event=>{if(typeof event.data==='string'&&event.data.length<=1024*1024)onMessage(event.data)};
    ws.onerror=()=>onError('WebSocket connection error');
    ws.onclose=event=>{
      onState('closed');
      if(!closed&&reconnect&&event.code!==1000&&event.code!==1008){
        const delay=Math.min(30000,500*Math.pow(2,Math.min(attempt++,6)));
        timer=setTimeout(start,delay);
      }
    };
  }
  start();
  return {
    send(value){if(closed)throw Error('Noqeri WebSocket is closed');
      if(typeof value!=='string'||value.length>1024*1024)throw Error('Noqeri WebSocket accepts text up to 1 MiB');
      if(ws?.readyState===1){ws.send(value);return true}
      if(pending.length>=maxQueue)return false;
      pending.push(value);return true
    },
    close(){closed=true;clearTimeout(timer);pending.length=0;ws?.close(1000,'');onState('closed')}
  };
}
