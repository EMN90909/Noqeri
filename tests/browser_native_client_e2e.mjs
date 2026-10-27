// Native C++ view compiler runs without Node; Node is test harness only.
import assert from 'node:assert/strict';
import {execFile,spawn} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve} from 'node:path';
import {createServer} from 'node:net';
import {setTimeout as delay} from 'node:timers/promises';
import {chromium} from 'playwright';

const execute=promisify(execFile);
const executable=resolve(process.argv[2]||'./build-browser/noqeri-web');
const project=resolve(process.argv[3]||'./examples/native_web_client');
const temp=await mkdtemp(join(tmpdir(),'noqeri-native-csr-browser-'));
let staticServer=null,browser=null;
try {
  await execute(executable,['build-client',project,join(temp,'dist')],{
    env:{...process.env,PATH:'/no-node-available'},timeout:20000
  });
  const probe=createServer();
  await new Promise(resolve=>probe.listen(0,'127.0.0.1',resolve));
  const port=probe.address().port;
  await new Promise(resolve=>probe.close(resolve));
  staticServer=spawn('python3',['-m','http.server',String(port),'--bind','127.0.0.1',
    '--directory',join(temp,'dist')],{stdio:['ignore','ignore','pipe']});
  const origin='http://127.0.0.1:'+port;
  for(let i=0;i<60;i++){
    try{if((await fetch(origin+'/')).status===200)break;}catch{}
    if(i===59)throw Error('native CSR assets did not start');
    await delay(75);
  }
  browser=await chromium.launch({headless:true});
  const nojs=await browser.newContext({javaScriptEnabled:false});
  const pageNojs=await nojs.newPage();
  assert.equal((await pageNojs.goto(origin+'/')).status(),200);
  assert.equal(await pageNojs.getByText('Count: 0').count(),1);
  assert.equal((await pageNojs.goto(origin+'/about/')).status(),200);
  assert.equal(await pageNojs.getByRole('heading',{name:'Client-side routing without Node.js'}).count(),1);
  await nojs.close();

  const page=await browser.newPage();
  const errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  const response=await page.goto(origin+'/',{waitUntil:'load'});
  assert.equal(response.status(),200);
  await page.evaluate(()=>{window.__nq_root=document.querySelector('#app > div');window.__nq_identity={};});
  await page.getByRole('button',{name:'Add one'}).click();
  await page.getByText('Count: 1').waitFor();
  assert.equal(await page.evaluate(()=>window.__nq_root===document.querySelector('#app > div')),true);
  await page.getByRole('link',{name:'About'}).click();
  await page.getByRole('heading',{name:'Client-side routing without Node.js'}).waitFor();
  assert.equal(new URL(page.url()).pathname,'/about');
  assert.equal(await page.evaluate(()=>window.__nq_root===document.querySelector('#app > div')),true);
  assert.equal(await page.evaluate(()=>Boolean(window.__nq_identity)),true,'unexpected reload');
  await page.getByRole('link',{name:'Home'}).click();
  await page.getByText('Count: 1').waitFor();
  assert.equal(new URL(page.url()).pathname,'/');
  await page.getByRole('button',{name:'Save a click'}).click();
  await page.getByText('Saved: 1').waitFor();
  await page.reload({waitUntil:'load'});
  await page.getByText('Saved: 1').waitFor();
  assert.equal(await page.getByText('Count: 0').count(),1,
    'in-memory state should reset while persisted preferences survive');
  assert.deepEqual(errors,[]);
  console.log('PASS: native no-Node C++ view compiler, SSR first paint, client state and SPA navigation in Chromium');
}finally{
  await browser?.close();
  staticServer?.kill('SIGTERM');
  await rm(temp,{recursive:true,force:true});
}
