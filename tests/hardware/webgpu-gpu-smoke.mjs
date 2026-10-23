import { chromium } from 'playwright'
import { mkdir, writeFile } from 'node:fs/promises'
import { join } from 'node:path'

const origin=process.env.NOQERI_GPU_ORIGIN || 'http://127.0.0.1:8765'
const proofDir=process.env.NOQERI_GPU_EVIDENCE_DIR || 'build/gpu-qualification'
const browser=await chromium.launch({
  headless:true,
  args:[
    '--enable-unsafe-webgpu',
    '--enable-features=Vulkan,WebGPU',
    '--disable-software-rasterizer',
    '--use-gl=angle'
  ]
})
let report
try {
  const page=await browser.newPage()
  await page.goto(origin+'/examples/shaders/webgpu_demo.html')
  report=await page.evaluate(async()=>{
    const adapter=await navigator.gpu?.requestAdapter({
      powerPreference:'high-performance',forceFallbackAdapter:false
    })
    if(!adapter)throw new Error('NQR-GPU-HW1: no WebGPU adapter available')
    const info=adapter.info || {}
    const vendor=String(info.vendor||'')
    const descriptor=String(info.description||'')+' '+String(info.device||'')
    const model=(vendor+' '+descriptor).toLowerCase()
    if(adapter.isFallbackAdapter===true ||
       /swiftshader|llvmpipe|softpipe|software|microsoft basic|warp|google|virtual gpu|mesa offscreen/.test(model) ||
       !/nvidia|amd|advanced micro devices|intel|apple|arm|qualcomm|imagination|samsung/.test(vendor.toLowerCase()))
      throw new Error('NQR-GPU-HW2: hardware attestation rejected GPU adapter: '+vendor+' '+descriptor)

    const host=await import('/examples/shaders/webgpu_host.mjs')
    const physicalGPU={
      requestAdapter:async()=>adapter,
      getPreferredCanvasFormat:()=>navigator.gpu.getPreferredCanvasFormat()
    }
    const shader=async file=>{
      const response=await fetch('/build/'+file)
      if(!response.ok)throw new Error('Missing compiled shader: '+file)
      return response.text()
    }
    const checked=(a,b,epsilon=0.00001)=>{
      if(Math.abs(a-b)>epsilon)throw new Error('GPU result mismatch '+a+' vs '+b)
    }
    const inverse=await host.runNoqeriCompute(
      await shader('invert.wgsl'),
      new Uint32Array([0,1,255,4294967295]),physicalGPU)
    const expectedUint=[4294967295,4294967294,4294967040,0]
    if(inverse.length!==expectedUint.length)throw new Error('Bad u32 compute output length')
    inverse.forEach((v,i)=>{
      if(v!==expectedUint[i])throw new Error('Bad u32 GPU result at index '+i)
    })
    const halved=await host.runNoqeriCompute(
      await shader('float-compute.wgsl'),
      new Float32Array([-4,0,1,2,8]),physicalGPU)
    const expectedFloat=[0,0,0.5,1,4]
    halved.forEach((v,i)=>checked(v,expectedFloat[i]))

    const vector=await host.runNoqeriCompute(
      await shader('vector-compute.wgsl'),
      new Float32Array([1,0,0,1,0,1,0,1]),physicalGPU)
    const expectedVector=[0.85,0.05,0.025,1,0.10,0.80,0.025,1]
    vector.forEach((v,i)=>checked(v,expectedVector[i]))

    const canvas=document.createElement('canvas')
    canvas.width=32;canvas.height=32
    document.body.append(canvas)
    const texturePixels=new Uint8Array([
      255,0,0,255,0,255,0,255,
      0,0,255,255,255,255,255,255
    ])
    const release=await host.renderNoqeriFragment(
      canvas,await shader('texture.wgsl'),physicalGPU,
      {width:2,height:2,data:texturePixels})
    release()

    // Verify actual GPU render readback on a 2x2 non-swapchain texture.
    const device=await adapter.requestDevice()
    let inputTexture,outputTexture,readback
    try {
      const module=device.createShaderModule({code:await shader('texture-load.wgsl')})
      const messages=await module.getCompilationInfo()
      if(messages.messages.some(x=>x.type==='error'))
        throw new Error('GPU driver rejected generated Noqeri texture WGSL')
      const vertex=device.createShaderModule({code:[
        '@vertex fn fullscreen(@builtin(vertex_index) index: u32) -> @builtin(position) vec4<f32> {',
        '  let positions = array<vec2<f32>, 3>(vec2<f32>(-1.0,-1.0),vec2<f32>(3.0,-1.0),vec2<f32>(-1.0,3.0));',
        '  return vec4<f32>(positions[index],0.0,1.0);',
        '}'
      ].join('\n')})
      const pipeline=device.createRenderPipeline({
        layout:'auto',
        vertex:{module:vertex,entryPoint:'fullscreen'},
        fragment:{module,entryPoint:'main',targets:[{format:'rgba8unorm'}]},
        primitive:{topology:'triangle-list'}
      })
      inputTexture=device.createTexture({
        size:{width:2,height:2},format:'rgba8unorm',
        usage:GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.COPY_DST
      })
      const padded=new Uint8Array(512)
      padded.set(texturePixels.subarray(0,8),0)
      padded.set(texturePixels.subarray(8,16),256)
      device.queue.writeTexture({texture:inputTexture},padded,
        {bytesPerRow:256,rowsPerImage:2},{width:2,height:2,depthOrArrayLayers:1})
      outputTexture=device.createTexture({
        size:{width:2,height:2},format:'rgba8unorm',
        usage:GPUTextureUsage.RENDER_ATTACHMENT|GPUTextureUsage.COPY_SRC
      })
      readback=device.createBuffer({size:512,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ})
      const bind=device.createBindGroup({
        layout:pipeline.getBindGroupLayout(0),
        entries:[{binding:0,resource:inputTexture.createView()}]
      })
      const command=device.createCommandEncoder()
      const pass=command.beginRenderPass({
        colorAttachments:[{
          view:outputTexture.createView(),
          loadOp:'clear',storeOp:'store',
          clearValue:{r:0,g:0,b:0,a:1}
        }]
      })
      pass.setPipeline(pipeline);pass.setBindGroup(0,bind);pass.draw(3);pass.end()
      command.copyTextureToBuffer(
        {texture:outputTexture},
        {buffer:readback,bytesPerRow:256,rowsPerImage:2},
        {width:2,height:2,depthOrArrayLayers:1})
      device.queue.submit([command.finish()])
      await readback.mapAsync(GPUMapMode.READ)
      const pixels=new Uint8Array(readback.getMappedRange())
      for(let y=0;y<2;y++)for(let x=0;x<2;x++){
        const actual=Array.from(pixels.subarray(y*256+x*4,y*256+x*4+4))
        const expected=Array.from(texturePixels.subarray((y*2+x)*4,(y*2+x+1)*4))
        for(let c=0;c<4;c++)if(Math.abs(actual[c]-expected[c])>2)
          throw new Error('Texture GPU pixel mismatch at '+x+','+y+': '+actual+' vs '+expected)
      }
      readback.unmap()
    } finally {
      if(readback){try{readback.unmap()}catch{}readback.destroy()}
      inputTexture?.destroy()
      outputTexture?.destroy()
      device.destroy()
    }
    return {
      gpuVendor:vendor, gpuDevice:String(info.device||''),
      gpuArchitecture:String(info.architecture||''),
      gpuDescription:descriptor,
      isFallbackAdapter:adapter.isFallbackAdapter===true,
      supported:['uint32-compute','float32-compute','vec4-compute',
        'sampled-2D-texture','2D-texel-fetch-pixel-readback'],
      hardwareReadback:'PASS',
      browser:navigator.userAgent
    }
  })
  if(report.isFallbackAdapter || report.hardwareReadback!=='PASS')
    throw new Error('Hardware qualification was not proven.')
  await mkdir(proofDir,{recursive:true})
  const result={
    date:new Date().toISOString(),
    commit:process.env.GITHUB_SHA || '',
    workflow:process.env.GITHUB_RUN_ID || '',
    qualification:'hardware-backed Chrome WebGPU compute, vector and texture fragment',
    ...report
  }
  await writeFile(join(proofDir,'webgpu-hardware-proof.json'),
    JSON.stringify(result,null,2)+'\n',{mode:0o644})
  console.log('Noqeri physical GPU hardware smoke PASS:',report.gpuVendor,report.gpuDevice)
} finally {
  await browser.close()
}
