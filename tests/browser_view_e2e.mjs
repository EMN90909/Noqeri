// Real browser tests for experimental Noqeri view static prerender + hydration.
// Playwright and Node are TEST/BUILD hosts only, never browser runtime dependencies.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {mkdtemp,rm} from 'node:fs/promises';
import {createServer} from 'node:net';
import {tmpdir} from 'node:os';
import {join,resolve} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';
import {chromium} from 'playwright';
import {buildWebProject} from '../Runtime/web-project.mjs';

const source=resolve(process.argv[2]||'./examples/native_web');
const temp=await mkdtemp(join(tmpdir(),'noqeri-view-browser-'));
const output=join(temp,'dist');
const socket=createServer();
await new Promise(resolve=>socket.listen(0,'127.0.0.1',resolve));
const port=socket.address().port;
await new Promise(resolve=>socket.close(resolve));
let server=null,browser=null;
try {
  await buildWebProject({source,out:output});
  const root='http://127.0.0.1:'+port;
  server=spawn('python3',['-m','http.server',String(port),'--bind','127.0.0.1',
    '--directory',output],{stdio:['ignore','ignore','pipe']});
  let logs='';
  server.stderr.on('data',data=>{if(logs.length<4000)logs+=data.toString()});
  for(let i=0;i<60;i++) {
    if(server.exitCode!==null)throw Error('static server exited: '+logs);
    try {if((await fetch(root+'/')).status===200)break;} catch{}
    if(i===59)throw Error('static server could not start: '+logs);
    await delay(70);
  }
  browser=await chromium.launch({headless:true});
  const staticContext=await browser.newContext({javaScriptEnabled:false});
  const staticPage=await staticContext.newPage();
  assert.equal((await staticPage.goto(root+'/',{waitUntil:'load'})).status(),200);
  assert.equal(await staticPage.getByRole('heading',{name:'Build the web with Noqeri'}).count(),1);
  assert.equal(await staticPage.getByText('Clicks: 0').count(),1);
  assert.equal((await staticPage.goto(root+'/about/',{waitUntil:'load'})).status(),200);
  assert.equal(await staticPage.getByRole('heading',{name:'About Noqeri Web'}).count(),1);
  await staticContext.close();

  const context=await browser.newContext();
  const page=await context.newPage();
  const errors=[];
  page.on('pageerror',error=>errors.push(error.message));
  assert.equal((await page.goto(root+'/',{waitUntil:'load'})).status(),200);
  const wrapper=page.locator('#app > div');
  assert.equal(await wrapper.count(),1);
  await page.evaluate(()=>{window.__noqeriWrapper=document.querySelector('#app > div');window.__pageToken={};});
  await page.getByRole('button',{name:'Count up'}).click();
  await page.getByText('Clicks: 1').waitFor({timeout:6000});
  assert.equal(await page.evaluate(()=>window.__noqeriWrapper===document.querySelector('#app > div')),true);
  await page.getByRole('link',{name:'About',exact:true}).click();
  await page.getByRole('heading',{name:'About Noqeri Web'}).waitFor();
  assert.equal(new URL(page.url()).pathname,'/about');
  assert.equal(await page.evaluate(()=>Boolean(window.__pageToken)),true,'SPA navigation unexpectedly reloaded');
  await page.evaluate(()=>history.back());
  await page.getByText('Clicks: 1').waitFor();
  assert.equal(await page.evaluate(()=>document.querySelector('#app > div')===window.__noqeriWrapper),true);
  assert.deepEqual(errors,[]);
  console.log('PASS: Noqeri view SSR prerender visible without JS; live DOM hydration, state, SPA routing');
  await context.close();
} finally {
  await browser?.close();
  server?.kill('SIGTERM');
  await rm(temp,{recursive:true,force:true});
}
