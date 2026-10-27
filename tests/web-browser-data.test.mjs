import test from 'node:test'
import assert from 'node:assert/strict'
import {signal,validate,request,resource,session,socket} from '../Runtime/noqeri-web-app.mjs'
import {compileView} from '../Runtime/web-view-compiler.mjs'
import {eventInfo} from '../Runtime/noqeri-web-runtime.mjs'
import {buildWebProject} from '../Runtime/web-project.mjs'
import {mkdtemp,mkdir,readFile,writeFile,rm} from 'node:fs/promises'
import {join} from 'node:path'
import {tmpdir} from 'node:os'
test('reactive signals and validation report understandable errors',()=>{
  const value=signal(1),observed=[];
  const unsubscribe=value.subscribe(v=>observed.push(v));
  value.set(n=>n+1);unsubscribe();value.set(10);
  assert.equal(value.get(),10);assert.deepEqual(observed,[2]);
  let check=validate({email:'oops',password:'abc'}, {email:{required:true,email:true},password:{required:true,minLength:8}});
  assert.equal(check.ok,false);
  assert.match(check.errors.email,/email/);
  assert.match(check.errors.password,/8/);
  assert.equal(validate({name:'Ada'},{name:{required:true,maxLength:10}}).ok,true);
});
test('same-origin requests, resources and session logout',async()=>{
  const requests=[];
  const mock=async(path,options)=>{requests.push({path,options});return {
    ok:true,status:200,text:async()=>JSON.stringify(options.method==='DELETE'?{}:path==='/api/session'?{user:{id:'x'},csrf:'token'}:{name:'Ada'})
  }};
  await assert.rejects(request('//evil.test',{fetchImpl:mock}),/same-origin/);
  await assert.rejects(request('/%2e%2e/admin',{fetchImpl:mock}),/same-origin/);
  await request('/api/tasks',{method:'POST',body:{name:'Ada'},csrf:'token',fetchImpl:mock});
  assert.equal(requests[0].options.headers['X-CSRF-Token'],'token');
  assert.equal(requests[0].options.credentials,'same-origin');
  const res=resource('/api/tasks',{fetchImpl:mock});await res.refresh();
  assert.equal(res.get().data.name,'Ada');res.dispose();
  const auth=session('/api/session',{fetchImpl:mock});await auth.refresh();
  assert.equal(auth.get().user.id,'x');await auth.logout();
  assert.equal(auth.get().user,null);
  assert.equal(requests.at(-1).options.method,'DELETE');
});
test('browser WebSocket is same-origin, text bounded and cleanly closed',()=>{
  const sockets=[];
  class FakeWebSocket{
    constructor(url){this.url=url;this.readyState=0;this.sent=[];sockets.push(this)}
    send(v){this.sent.push(v)}
    close(code){this.closeCode=code;this.readyState=3}
  }
  const events=[];
  const ws=socket('/live',{WebSocketImpl:FakeWebSocket,locationImpl:{protocol:'https:',host:'app.example'},onMessage:t=>events.push(t),reconnect:false});
  assert.equal(sockets[0].url,'wss://app.example/live');
  assert.equal(ws.send('queued'),true);
  sockets[0].readyState=1;sockets[0].onopen();
  assert.deepEqual(sockets[0].sent,['queued']);
  sockets[0].onmessage({data:'hello'});
  assert.deepEqual(events,['hello']);
  ws.close();
  assert.equal(sockets[0].closeCode,1000);
  assert.throws(()=>socket('wss://evil.test',{WebSocketImpl:FakeWebSocket,locationImpl:{protocol:'https:',host:'app.example'}}),/same-origin/);
});
test('Noqeri-only views support reactive input and form value event expressions',()=>{
  const source='let name = ""\nlet status = ""\nview { form on.submit { status = event.form.email } { input name: "email" type: "email" required: true on.input { name = event.value } {} button type: "submit" { "Save" } } p { name } p { status } }';
  const result=compileView(source);
  assert.match(result,/__eventInfo/);
  assert.match(result,/const event=__eventInfo/);
  assert.match(result,/__member\(event,"form"\)/);
  assert.throws(()=>compileView('let name = event.value\nview { p { name } }'),/undeclared name event/);
  assert.throws(()=>compileView('view { p { event.value } }'),/undeclared name event/);
  const e=eventInfo({type:'input',target:{value:'A',checked:false}});
  assert.equal(e.value,'A');assert.equal(e.checked,false);
});
test('Noqeri build bundles the built-in app module only when imported',async()=>{
  const tmp=await mkdtemp(join(tmpdir(),'noqeri-app-'));
  try{
    const src=join(tmp,'src'),out=join(tmp,'out');await mkdir(src);
    await writeFile(join(src,'main.noqeri'),'import "noqeri/app" as app\nlet title = "Hello"\nroute "/" { h1 { title } }');
    await buildWebProject({source:src,out});
    assert.match(await readFile(join(out,'noqeri/app.mjs'),'utf8'),/export function socket/);
    assert.match(await readFile(join(out,'main.mjs'),'utf8'),/\.\/noqeri\/app.mjs/);
  }finally{await rm(tmp,{recursive:true,force:true})}
});
