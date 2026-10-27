// Real Chromium coverage for native Node-free SSR plus browser enhancement.
// Playwright is test-only; the generated application never needs Node.js.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {mkdtemp,rm} from 'node:fs/promises';
import {createServer} from 'node:net';
import {tmpdir} from 'node:os';
import {join,resolve} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';
import {chromium} from 'playwright';

const executable=resolve(process.argv[2]||'./build/noqeri-web');
const source=resolve(process.argv[3]||'./examples/native_web_actions');
const temp=await mkdtemp(join(tmpdir(),'noqeri-reactive-browser-'));
const socket=createServer();
await new Promise(resolve=>socket.listen(0,'127.0.0.1',resolve));
const port=socket.address().port;
await new Promise(resolve=>socket.close(resolve));
const origin='http://127.0.0.1:'+port;
const server=spawn(executable,['serve-app',source,String(port),join(temp,'state.nqkv')],{
  stdio:['ignore','ignore','pipe'],env:{...process.env,NOQERI_WEB_REACTIVE:'1'}
});
let errors='';
server.stderr.on('data',data=>{if(errors.length<12000)errors+=data.toString()});
let browser;
try {
  for(let i=0;i<80;i++){
    if(server.exitCode!==null)throw Error('Native server exited: '+errors);
    try {if((await fetch(origin+'/theme.css')).status===200)break;} catch{}
    if(i===79)throw Error('Native server startup failed: '+errors);
    await delay(80);
  }
  browser=await chromium.launch({headless:true});
  const page=await browser.newPage();
  const diagnostics=[];
  page.on('pageerror',error=>diagnostics.push('pageerror: '+error.message));
  page.on('console',message=>{if(message.type()==='error')diagnostics.push('console: '+message.text())});
  page.on('requestfailed',request=>diagnostics.push('request failed: '+request.url()+' '+request.failure()?.errorText));
  page.on('response',response=>{
    if(response.url().includes(origin))diagnostics.push('response '+response.status()+' '+response.request().method()+' '+response.url())
  });
  const response=await page.goto(origin+'/',{waitUntil:'load'});
  assert.equal(response.status(),200);
  assert.match(response.headers()['content-security-policy'],/connect-src 'self'/);
  await page.waitForFunction(()=>document.documentElement.dataset.noqeriHydrated==='1');
  await page.evaluate(()=>{window.__noqeriMain=document.querySelector('main')});
  await page.getByRole('button',{name:'Increment without JavaScript'}).click();
  try {
    await page.getByText('Your clicks: 1').waitFor({timeout:6000});
  } catch (error) {
    console.error('Reactive click diagnostics:',JSON.stringify({
      body:(await page.locator('body').innerText()).slice(0,1000),
      events:diagnostics.slice(-20)
    }));
    throw error;
  }
  assert.equal(await page.evaluate(()=>document.querySelector('main')===window.__noqeriMain),true,
    'reactive POST must reuse unchanged main DOM rather than hard reload');
  await page.getByRole('link',{name:'About'}).click();
  await page.getByText('Server-driven Noqeri').waitFor();
  assert.equal(new URL(page.url()).pathname,'/about');
  assert.equal(await page.evaluate(()=>document.querySelector('main')===window.__noqeriMain),true,
    'client-side navigation should keep matching layout node');
  await page.evaluate(()=>history.back());
  await page.getByText('Your clicks: 1').waitFor();
  const injection='<img src=x onerror=alert(1)>';
  await page.getByRole('textbox',{name:'Your name'}).fill(injection);
  await page.getByRole('button',{name:'Save name'}).click();
  await page.getByText('Hello, '+injection).waitFor();
  assert.equal(await page.locator('img[onerror]').count(),0);
  assert.equal(await page.evaluate(()=>document.querySelector('main')===window.__noqeriMain),true);
  // Real-browser, same-origin GET navigation latency. This measures network +
  // parsing + DOM reconciliation, not a synthetic JS loop.
  const elapsed=await page.evaluate(async()=>{
    const samples=[];
    for(let i=0;i<32;i++) {
      const target=i%2===0?'/about':'/';
      const link=document.querySelector('a[href="'+target+'"]');
      if(!link)throw Error('missing native route link '+target);
      const begin=performance.now();
      await new Promise((resolve,reject)=>{
        const timeout=setTimeout(()=>reject(Error('SPA navigation timed out')),2500);
        document.addEventListener('noqeri:updated',()=>{
          clearTimeout(timeout);resolve();
        },{once:true});
        link.click();
      });
      if(location.pathname!==target)throw Error('history pathname differs: '+location.pathname);
      samples.push(performance.now()-begin);
    }
    if(performance.getEntriesByType('navigation').length!==1)
      throw Error('unexpected full-page document navigation');
    if(document.querySelector('main')!==window.__noqeriMain)
      throw Error('SPA replaced the existing main layout element');
    return samples;
  });
  const sorted=elapsed.toSorted((a,b)=>a-b);
  const p50=sorted[Math.ceil(sorted.length*.5)-1];
  const p95=sorted[Math.ceil(sorted.length*.95)-1];
  const budget=Number(process.env.NOQERI_BROWSER_NAV_P95_MS||800);
  assert.ok(Number.isFinite(budget)&&p95<budget,
    'native browser navigation p95='+p95.toFixed(2)+'ms exceeds '+budget+'ms');
  console.log('NOQERI_BROWSER_PERFORMANCE',JSON.stringify({
    samples:elapsed.length,metric:'SSR navigation + DOM reconcile',
    p50_ms:Number(p50.toFixed(2)),p95_ms:Number(p95.toFixed(2)),budget_ms:budget
  }));
  console.log('PASS: native Noqeri SSR hydration, same-document navigation, stateful forms and XSS regression');
} finally {
  await browser?.close();
  server.kill('SIGTERM');
  await rm(temp,{recursive:true,force:true});
}
