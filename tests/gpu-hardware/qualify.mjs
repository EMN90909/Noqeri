import { chromium } from 'playwright-core'
import { writeFile, mkdir } from 'node:fs/promises'
import { resolve } from 'node:path'

const executablePath=process.env.NOQERI_GPU_CHROMIUM
if(!executablePath)throw new Error('NOQERI_GPU_CHROMIUM must be a physical-GPU-capable Chromium executable')
const url=process.env.NOQERI_GPU_ORIGIN||'http://127.0.0.1:8000'
const browser=await chromium.launch({
  executablePath,headless:true,
  args:['--enable-features=Vulkan,WebGPUDeveloperFeatures']
})
const evidenceDir=resolve(process.env.NOQERI_GPU_EVIDENCE||'build/gpu-qualification')
await mkdir(evidenceDir,{recursive:true})
try {
  const page=await browser.newPage({viewport:{width:900,height:600}})
  await page.goto(new URL('/examples/shaders/webgpu_demo.html',url).href,{waitUntil:'load'})
  await page.waitForFunction(()=>{
    const result=document.querySelector('#result')?.textContent??''
    return result.includes('rendered with WebGPU.')||result.includes('Noqeri fragment shader rendered using WebGPU.')
  },{timeout:25000})
  const result=await page.evaluate(async()=>{
    const gpu=navigator.gpu
    if(!gpu)throw new Error('navigator.gpu unavailable')
    const adapter=await gpu.requestAdapter({powerPreference:'high-performance'})
    if(!adapter||adapter.isFallbackAdapter!==false)
      throw new Error('GPU qualification requires a real hardware adapter (fallback adapters fail)')
    const info=adapter.info
    if(!info||!info.vendor)
      throw new Error('GPU driver vendor not reported')
    const {runNoqeriCompute,renderNoqeriFragment}=await import('/examples/shaders/webgpu_host.mjs')
    async function shader(name) {
      const response=await fetch('/build/'+name,{cache:'no-store'})
      if(!response.ok)throw new Error('Missing generated shader: '+name)
      return response.text()
    }
    const integer=await runNoqeriCompute(await shader('invert.wgsl'),
      new Uint32Array([0,1,255,0xffffffff]),gpu)
    const expected=[0xffffffff,0xfffffffe,0xffffff00,0]
    if(integer.length!==expected.length || integer.some((x,i)=>x!==expected[i]))
      throw new Error('Physical GPU integer compute mismatch')
    const fp=await runNoqeriCompute(await shader('float-compute.wgsl'),
      new Float32Array([-4,0,1,2,8]),gpu)
    const expectedFloat=[0,0,0.5,1,4]
    if(fp.length!==expectedFloat.length||fp.some((x,i)=>Math.abs(x-expectedFloat[i])>0.0001))
      throw new Error('Physical GPU float compute mismatch')
    const packed=await runNoqeriCompute(await shader('vector-compute.wgsl'),
      new Float32Array([1,2,4,8,2,4,8,16]),gpu)
    const expectedVector=[1,1,1,8,2,2,2,16]
    if(packed.some((x,i)=>Math.abs(x-expectedVector[i])>0.0001))
      throw new Error('Physical GPU vec4 compute mismatch')
    const canvas=document.querySelector('#frame')
    const pixels=new Uint8Array([
      255,0,0,255, 0,255,0,255,
      0,0,255,255, 255,255,255,255
    ])
    const release=await renderNoqeriFragment(canvas,
      await shader('textured-fragment.wgsl'),gpu,{pixels,width:2,height:2})
    release()
    return {
      fallback:adapter.isFallbackAdapter,
      vendor:info.vendor,
      architecture:info.architecture??'',
      device:info.device??'',
      description:info.description??'',
      integer:[...integer],float:[...fp],vector:[...packed],
      texture:true,fragment:true
    }
  })
  if(result.fallback!==false)throw new Error('GPU fallback adapter incorrectly accepted')
  await page.screenshot({path:resolve(evidenceDir,'noqeri-gpu-hardware.png'),fullPage:true})
  const evidence={
    ...result,
    verifiedPhysicalAdapter:true,
    commit:process.env.GITHUB_SHA||'manual',
    runner:process.env.RUNNER_NAME||'local',
    generatedAt:new Date().toISOString(),
    hardwareQualified:true,
    note:'Physical browser WebGPU compute and texture/fragment tests passed; this does not qualify other vendors or full Noqeri Stage-2 parity'
  }
  await writeFile(resolve(evidenceDir,'noqeri-gpu-evidence.json'),JSON.stringify(evidence,null,2)+'\n')
  console.log('Noqeri physical GPU qualification passed:',evidence.vendor,evidence.device)
} finally {
  await browser.close()
}
