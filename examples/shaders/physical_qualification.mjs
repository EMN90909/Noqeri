import {runNoqeriCompute, renderNoqeriFragment} from './webgpu_host.mjs'

// Run only on an actual secure-context WebGPU browser and record GPU driver info.
// This is deliberately not invoked in headless/mocked CI as hardware evidence.
export async function qualifyPhysicalGpu(canvas, base='/build') {
  if (!globalThis.isSecureContext || !navigator.gpu)
    throw new Error('A secure-context browser with an actual WebGPU adapter is required.')
  const adapter=await navigator.gpu.requestAdapter({powerPreference:'high-performance'})
  if (!adapter || adapter.isFallbackAdapter === true)
    throw new Error('No non-fallback WebGPU adapter was available.')
  const info=adapter.info ?? {}
  const evidence={
    timestamp:new Date().toISOString(),
    userAgent:navigator.userAgent,
    gpuVendor:info.vendor || 'not disclosed',
    gpuArchitecture:info.architecture || 'not disclosed',
    gpuDevice:info.device || 'not disclosed',
    gpuDescription:info.description || 'not disclosed',
    isFallbackAdapter:adapter.isFallbackAdapter ?? 'unknown',
    runtime:'browser-webgpu-physical-device',
    vectorCompute:false,
    texturedFragment:false
  }
  const get=async file=>{
    const result=await fetch(base+'/'+file,{cache:'no-store'})
    if(!result.ok)throw new Error('Missing compiled WGSL: '+file+' (HTTP '+result.status+')')
    return result.text()
  }
  const vector=await get('noqeri-vector-compute.wgsl')
  const input=new Float32Array([0,0.5,1,1,0.25,0.75,0,0.5])
  const output=await runNoqeriCompute(vector,input,navigator.gpu)
  const expected=[1,0.5,0,1,0.75,0.25,1,0.5]
  for(let i=0;i<expected.length;i++)
    if(Math.abs(output[i]-expected[i])>0.00001)
      throw new Error('Physical GPU vector compute mismatch at lane '+i+': '+output[i])
  evidence.vectorCompute=true
  const texture=await get('noqeri-texture-fragment.wgsl')
  const rgba=new Uint8Array([255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255])
  const release=await renderNoqeriFragment(canvas,texture,navigator.gpu,
    {width:2,height:2,data:rgba})
  evidence.texturedFragment=true
  // The framebuffer is presented for manual review, not used as a pixel-accurate assertion.
  release()
  return evidence
}
