// Browser end-to-end smoke test for the JavaScript-free native web renderer.
// Install optional developer dependency: npm install --no-save playwright@1.56.1
import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { mkdtemp, rm } from 'node:fs/promises'
import { createServer } from 'node:net'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import { setTimeout as delay } from 'node:timers/promises'
import { chromium } from 'playwright'

const executable=resolve(process.argv[2] || './build/noqeri-web')
const input=resolve(process.argv[3] || './examples/native_web_actions')
const root=await mkdtemp(join(tmpdir(),'noqeri-browser-e2e-'))
async function freePort() {
  const s=createServer()
  await new Promise((ok,no)=>s.once('error',no).listen(0,'127.0.0.1',ok))
  const port=s.address().port
  await new Promise(ok=>s.close(ok))
  return port
}
const port=await freePort()
const origin='http://127.0.0.1:'+port
const server=spawn(executable,['serve-app',input,String(port),join(root,'state.nqkv')],{
  stdio:['ignore','ignore','pipe'],env:{...process.env}
})
let logs=''
server.stderr.on('data',c=>{if(logs.length<15000)logs+=c.toString()})
let browser=null
try {
  for(let attempt=0;attempt<80;++attempt) {
    if(server.exitCode!==null)throw new Error('Native server exited: '+logs)
    try {if((await fetch(origin+'/theme.css')).status===200)break}
    catch {}
    if(attempt===79)throw new Error('Native server failed to start: '+logs)
    await delay(80)
  }
  browser=await chromium.launch({headless:true})
  const context=await browser.newContext({javaScriptEnabled:false})
  const page=await context.newPage()
  const response=await page.goto(origin+'/',{waitUntil:'load'})
  assert.equal(response.status(),200)
  assert.match(response.headers()['content-security-policy'],/default-src 'none'/)
  const cookies=await context.cookies(origin)
  assert.equal(cookies.length,1)
  assert.equal(cookies[0].sameSite,'Strict')
  assert.equal(cookies[0].httpOnly,true)
  assert.equal(await page.locator('script').count(),0)
  assert.match(await page.locator('body').innerText(),/Your clicks: 0/)
  await page.getByRole('button',{name:'Increment without JavaScript'}).click()
  await page.getByText('Your clicks: 1').waitFor()
  assert.match(await page.locator('body').innerText(),/All-time clicks: 1/)
  const injection='<img src=x onerror=alert(1)>'
  await page.locator('input[name="displayName"]').fill(injection)
  await page.getByRole('button',{name:'Save name'}).click()
  await page.getByText('Hello, '+injection).waitFor()
  assert.equal(await page.locator('img[onerror]').count(),0)
  await page.getByRole('link',{name:'About'}).click()
  await page.getByText('Server-driven Noqeri').waitFor()
  await page.getByRole('link',{name:'Home'}).click()
  await page.getByText('Your clicks: 1').waitFor()
  const guest=await browser.newContext({javaScriptEnabled:false})
  const guestPage=await guest.newPage()
  await guestPage.goto(origin+'/')
  assert.match(await guestPage.locator('body').innerText(),/Your clicks: 0/)
  assert.match(await guestPage.locator('body').innerText(),/All-time clicks: 1/)
  await guest.close()
  await context.close()
  console.log('PASS: Chromium no-JS native pages, CSRF-backed forms, redirects, XSS escaping, session isolation')
} finally {
  if(browser)await browser.close()
  server.kill('SIGTERM')
  await rm(root,{recursive:true,force:true})
}
