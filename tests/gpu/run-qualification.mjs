// Run only on a physically GPU-equipped self-hosted runner.
// Requires Playwright and a real Chrome/Chromium installation. Never request
// SwiftShader/WARP or any software GPU flags to claim physical qualification.
import {mkdir,writeFile} from 'node:fs/promises'
import {resolve} from 'node:path'

const expectedVendor=process.env.NOQERI_GPU_VENDOR
const evidenceDir=resolve(process.env.NOQERI_GPU_REPORT_DIR||'build/gpu-hardware-evidence')
if(!expectedVendor)throw new Error('NOQERI_GPU_VENDOR is required for hardware verification')
const {chromium}=await import('playwright')
const browser=await chromium.launch({
  headless:false,
  ...(process.env.NOQERI_CHROME_BIN?{executablePath:process.env.NOQERI_CHROME_BIN}:{})
})
try{
  const page=await browser.newPage({viewport:{width:900,height:650}})
  const errors=[]
  page.on('pageerror',error=>errors.push(error.message))
  await page.goto('http://127.0.0.1:8765/tests/gpu/qualify.html?vendor='+
      encodeURIComponent(expectedVendor),{waitUntil:'domcontentloaded'})
  await page.waitForFunction(()=>{
    const report=window.__NOQERI_HARDWARE_REPORT__
    return report && report.status!=='running'
  },null,{timeout:120000})
  const report=await page.evaluate(()=>window.__NOQERI_HARDWARE_REPORT__)
  await mkdir(evidenceDir,{recursive:true})
  await page.screenshot({path:resolve(evidenceDir,'gpu-render.png'),fullPage:true})
  const output={...report,runner:{
    os:process.platform,arch:process.arch,
    ghRunId:process.env.GITHUB_RUN_ID||null,
    ghRunAttempt:process.env.GITHUB_RUN_ATTEMPT||null,
    ghSha:process.env.GITHUB_SHA||null
  },browserErrors:errors}
  await writeFile(resolve(evidenceDir,'gpu-evidence.json'),JSON.stringify(output,null,2)+'\n')
  if(report.status!=='pass'||errors.length||report.tests?.length!==5)
    throw new Error('Physical WebGPU qualification did not pass: '+JSON.stringify(output))
  console.log('Noqeri physical GPU qualification PASS:',JSON.stringify(output))
}finally{await browser.close()}
