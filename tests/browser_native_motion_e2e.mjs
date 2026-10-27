// Real Chromium qualification for native Noqeri-generated WAAPI animations.
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
const web=resolve(process.argv[2]||'./build-browser/noqeri-web');
const project=resolve('./examples/native_web_motion');
const temp=await mkdtemp(join(tmpdir(),'noqeri-motion-chromium-'));
let server=null,browser=null;
try{
 const output=join(temp,'dist');
 await execute(web,['build-client',project,output],{
   env:{...process.env,PATH:'/intentionally-no-node'},timeout:25000
 });
 const probe=createServer();
 await new Promise(ok=>probe.listen(0,'127.0.0.1',ok));
 const port=probe.address().port;
 await new Promise(ok=>probe.close(ok));
 server=spawn('python3',['-m','http.server',String(port),'--bind','127.0.0.1',
   '--directory',output],{stdio:['ignore','ignore','pipe']});
 const origin='http://127.0.0.1:'+port;
 for(let i=0;i<60;i++){
   try{if((await fetch(origin+'/')).status===200)break}catch{}
   if(i===59)throw Error('static Noqeri motion assets did not serve');
   await delay(80);
 }
 browser=await chromium.launch({headless:true});
 const noJS=await browser.newContext({javaScriptEnabled:false});
 const staticPage=await noJS.newPage();
 assert.equal((await staticPage.goto(origin+'/')).status(),200);
 assert.equal(await staticPage.getByRole('heading',{name:'Noqeri Motion'}).count(),1);
 await noJS.close();

 const page=await browser.newPage();
 const errors=[];
 page.on('pageerror',e=>errors.push(e.message));
 assert.equal((await page.goto(origin+'/',{waitUntil:'load'})).status(),200);
 await page.waitForFunction(()=>document.querySelector('.hero')?.getAnimations().length>0);
 await page.waitForFunction(()=>document.querySelector('.subtitle')?.getAnimations().length>0);
 const timeline=await page.evaluate(()=>{
   const hero=document.querySelector('.hero').getAnimations()[0].effect.getTiming();
   const first=document.querySelectorAll('.subtitle')[0].getAnimations()[0].effect.getTiming();
   const second=document.querySelectorAll('.subtitle')[1].getAnimations()[0].effect.getTiming();
   return {hero:hero.delay,copy:first.delay,stagger:second.delay};
 });
 assert.equal(timeline.hero,0);
 assert.equal(timeline.copy,280);
 assert.equal(timeline.stagger,340);
 assert.deepEqual(errors,[]);
 await page.close();

 const reduced=await browser.newContext({reducedMotion:'reduce'});
 const reducedPage=await reduced.newPage();
 await reducedPage.goto(origin+'/',{waitUntil:'load'});
 assert.equal(await reducedPage.locator('.hero').evaluate(el=>el.getAnimations().length),0);
 assert.equal(await reducedPage.getByText('Native timeline animations').count(),1);
 await reduced.close();
 console.log('PASS: native motion first paint, Chromium timelines/stagger and reduced-motion accessibility');
}finally{
 await browser?.close();
 server?.kill('SIGTERM');
 await rm(temp,{recursive:true,force:true});
}
