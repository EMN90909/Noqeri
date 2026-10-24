// Browser-side live WebGPU qualification on an actual adapter.
// Never invoke with --use-angle=swiftshader or forceFallbackAdapter: true.
// A GitHub artifact needs an authenticated self-hosted runner, plus operator
// confirmation that this adapter actually corresponds to the installed GPU.
import {runNoqeriCompute,renderNoqeriFragment} from '../../examples/shaders/webgpu_host.mjs'

const softwarePattern=/swiftshader|llvmpipe|softpipe|lavapipe|warp\b|software raster|cpu raster|software renderer/i
async function load(filename){
  const response=await fetch('/build/'+filename,{cache:'no-store'})
  if(!response.ok)throw new Error('Missing compiled Noqeri GPU shader: '+filename)
  return response.text()
}
function assertArray(actual,expected,epsilon=0){
  if(actual.length!==expected.length)throw new Error('Readback length mismatch')
  for(let i=0;i<actual.length;i++)
    if(!Number.isFinite(actual[i])||Math.abs(actual[i]-expected[i])>epsilon)
      throw new Error('GPU readback mismatch at '+i+': '+actual[i]+' expected '+expected[i])
}
export async function qualifyNoqeriGpu(canvas,{expectedVendor}={}){
  const started=new Date().toISOString()
  const gpu=globalThis.navigator?.gpu
  if(!gpu)throw new Error('No real WebGPU implementation is available')
  const adapter=await gpu.requestAdapter({powerPreference:'high-performance',forceFallbackAdapter:false})
  if(!adapter)throw new Error('A non-fallback WebGPU adapter was not provided')
  if(adapter.isFallbackAdapter===true)throw new Error('Fallback software GPU adapter is not physical hardware')
  const info=adapter.info || (typeof adapter.requestAdapterInfo==='function'
    ?await adapter.requestAdapterInfo():{})
  const details=Object.fromEntries(['vendor','architecture','device','description'].map(k=>[k,String(info[k]||'')]))
  const combined=Object.values(details).join(' ')
  if(softwarePattern.test(combined))throw new Error('Detected software WebGPU adapter: '+combined)
  if(!expectedVendor||!details.vendor||!details.vendor.toLowerCase().includes(expectedVendor.toLowerCase()))
    throw new Error('GPU vendor must be explicitly attested and match the browser adapter')
  const verifiedGpu={
    requestAdapter:async()=>adapter,
    getPreferredCanvasFormat:()=>gpu.getPreferredCanvasFormat()
  }
  const tests=[]
  {
    const result=await runNoqeriCompute(await load('gpu-invert.wgsl'),
      new Uint32Array([0,1,255,4294967295]),verifiedGpu)
    assertArray([...result],[4294967295,4294967294,4294967040,0])
    tests.push('integer-compute-readback')
  }
  {
    const result=await runNoqeriCompute(await load('gpu-float.wgsl'),
      new Float32Array([-4,0,1,2,8]),verifiedGpu)
    assertArray([...result],[0,0,0.5,1,4],1e-6)
    tests.push('float-compute-readback')
  }
  {
    const input=new Float32Array(4*2)
    input.set([1,0,0,1,0.2,0.4,0.6,0.8])
    const result=await runNoqeriCompute(await load('gpu-vector.wgsl'),input,verifiedGpu)
    if(result.length!==8||result.some(v=>!Number.isFinite(v)))
      throw new Error('Vector GPU readback contains invalid float data')
    tests.push('vec4-compute-readback')
  }
  const pixels=new Uint8Array(256*256*4)
  for(let y=0;y<256;y++)
    for(let x=0;x<256;x++){
      const i=(y*256+x)*4
      pixels[i]=x;pixels[i+1]=y;pixels[i+2]=128;pixels[i+3]=255
    }
  let release=null
  try{
    release=await renderNoqeriFragment(canvas,await load('gpu-texture.wgsl'),
      verifiedGpu,{width:256,height:256,pixels})
    tests.push('texture-upload-and-fragment-submit')
  }finally{release?.()}
  // Enforce native GPU device lifecycle rather than treating a software mock
  // as qualification. Device destruction must settle device.lost.
  const direct=await adapter.requestDevice()
  direct.destroy()
  const lost=await direct.lost
  if(!lost||lost.reason!=='destroyed')throw new Error('WebGPU device loss did not report expected destruction')
  tests.push('device-loss-lifecycle')
  return {
    schema:1,status:'pass',execution:'physical-device-candidate',
    started,completed:new Date().toISOString(),
    adapter:details,expectedVendor:String(expectedVendor),
    fallbackAdapter:false,
    ua:navigator.userAgent,
    tests,notes:'Driver identity must be corroborated with runner hardware inventory; this JSON is not a hardware attestation.'
  }
}
