import {runNoqeriCompute, renderNoqeriFragment, createNoqeriMeshRenderer3D, inspectNoqeriGpuCompatibility} from './webgpu_host.mjs'

// Run only on an actual secure-context WebGPU browser and record GPU driver info.
// This is deliberately not invoked in headless/mocked CI as hardware evidence.
let activeGpuRelease = null
let activeMeshRelease = null
export function releasePhysicalGpu() {
  activeGpuRelease?.()
  activeGpuRelease = null
  activeMeshRelease?.()
  activeMeshRelease = null
}
export async function qualifyPhysicalGpu(canvas, base='/build', meshCanvas=null) {
  releasePhysicalGpu()
  if (!globalThis.isSecureContext || !navigator.gpu)
    throw new Error('A secure-context browser with an actual WebGPU adapter is required.')
  const adapter=await navigator.gpu.requestAdapter({powerPreference:'high-performance'})
    ?? await navigator.gpu.requestAdapter()
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
    runtime:'browser-webgpu-hardware-not-independently-certified',
    deviceCapabilities:await inspectNoqeriGpuCompatibility(navigator.gpu),
    hardwareQualified:false,
    vectorCompute:false,
    texturedFragment:false,
    mesh3DDepth:false
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
  activeGpuRelease = release
  if(meshCanvas) {
    const [vertex,fragment]=await Promise.all([
      get('noqeri-vertex3d.wgsl'),get('noqeri-fragment3d.wgsl')
    ])
    const meshRenderer=await createNoqeriMeshRenderer3D(
      meshCanvas,vertex,fragment,navigator.gpu,6)
    activeMeshRelease=()=>meshRenderer.dispose()
    await meshRenderer.render(new Float32Array([
      -0.85,-0.85,0.8,1, 0.85,-0.85,0.8,1, 0,0.85,0.8,1,
      -0.45,-0.45,0.2,1, 0.45,-0.45,0.2,1, 0,0.45,0.2,1
    ]))
    evidence.mesh3DDepth=true
  }
  // Output is for manual inspection. This is NOT a pixel readback proof
  // and does not by itself qualify any physical GPU/driver combination.
  return evidence
}
