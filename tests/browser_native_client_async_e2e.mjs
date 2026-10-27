// End-to-end tests for native C++ codegen of async loading and browser forms.
// Node and Playwright run tests only, never the Noqeri app build process.
import assert from 'node:assert/strict';
import {execFile,spawn} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,mkdir,writeFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve} from 'node:path';
import {createServer} from 'node:net';
import {setTimeout as delay} from 'node:timers/promises';
import {chromium} from 'playwright';

const execute=promisify(execFile);
const compiler=resolve(process.argv[2]||'./build-browser/noqeri-web');
const temporary=await mkdtemp(join(tmpdir(),'noqeri-native-async-e2e-'));
let server=null,browser=null;
try {
  const source=join(temporary,'project');
  await mkdir(source);
  await writeFile(join(source,'main.noqeri'),[
    'let username = "Guest"',
    'load greeting from "/api/greeting.json"',
    'view { main {',
    ' h1 { "Node-free browser data" }',
    ' p { "Greeting: " + greeting.data.message }',
    ' p { "Name: " + username }',
    ' input type: "text" on.input { username = form.value } {}',
    ' form on.submit { username = form.username } {',
    '  input name: "username" type: "text" {}',
    '  button type: "submit" { "Save name" }',
    ' }',
    '} }'
  ].join('\n'));
  const output=join(temporary,'dist');
  await execute(compiler,['build-client',source,output],{
    env:{...process.env,PATH:'/intentionally-no-node'},timeout:25000
  });
  await mkdir(join(output,'api'),{recursive:true});
  await writeFile(join(output,'api/greeting.json'),JSON.stringify({message:'Hello from JSON'}));
  const probe=createServer();
  await new Promise(resolve=>probe.listen(0,'127.0.0.1',resolve));
  const port=probe.address().port;
  await new Promise(resolve=>probe.close(resolve));
  // The native loopback preview server must serve its own .mjs and JSON
  // assets. Node.js/Python are never involved in building or hosting the app.
  server=spawn(compiler,['serve',output,String(port)],{
    stdio:['ignore','ignore','pipe'],env:{...process.env,PATH:'/intentionally-no-node'}
  });
  const origin='http://127.0.0.1:'+port;
  for(let tries=0;tries<60;tries++){
    try{if((await fetch(origin+'/')).status===200)break}catch{}
    if(tries===59)throw Error('static server did not start');
    await delay(75);
  }
  browser=await chromium.launch({headless:true});
  const page=await browser.newPage();
  const errors=[];
  page.on('pageerror',error=>errors.push(error.message));
  const response=await page.goto(origin+'/',{waitUntil:'load'});
  assert.equal(response.status(),200);
  await page.getByText('Greeting: Hello from JSON').waitFor({timeout:10000});
  await page.evaluate(()=>{window.__nq_pageRoot=document.querySelector('#app > div')});
  const inputs=page.locator('input[type="text"]');
  assert.equal(await inputs.count(),2);
  await inputs.nth(0).fill('Alice');
  await page.getByText('Name: Alice').waitFor();
  assert.equal(await page.evaluate(()=>document.querySelector('#app > div')===window.__nq_pageRoot),true);
  await inputs.nth(1).fill('Bob');
  await page.getByRole('button',{name:'Save name'}).click();
  await page.getByText('Name: Bob').waitFor();
  assert.equal(await page.evaluate(()=>document.querySelector('#app > div')===window.__nq_pageRoot),true);
  assert.deepEqual(errors,[]);
  console.log('PASS: native Node-free client compiler, same-origin async JSON, reactive input and form submit in Chromium');
} finally {
  await browser?.close();
  server?.kill('SIGTERM');
  await rm(temporary,{recursive:true,force:true});
}
