import test from 'node:test'
import assert from 'node:assert/strict'
import {runNoqeriCompute, renderNoqeriFragment, renderNoqeriMesh3D, createNoqeriMeshRenderer3D, inspectNoqeriGpuCompatibility} from '../examples/shaders/webgpu_host.mjs'

globalThis.GPUBufferUsage = {STORAGE: 1, COPY_DST: 2, COPY_SRC: 4, MAP_READ: 8, VERTEX: 16}
globalThis.GPUMapMode = {READ: 1}
globalThis.GPUTextureUsage = {TEXTURE_BINDING: 1, COPY_DST: 2, RENDER_ATTACHMENT: 4}

function gpuMock({compileError = false, limits = {}} = {}) {
  const events={dispatches:[],buffers:[],textures:[],uploads:[],pipelines:[],draws:[],destroyed:0,configured:0,unconfigured:0,submitted:0}
  const context={
    configure(){events.configured++},
    unconfigure(){events.unconfigured++},
    getCurrentTexture(){return {createView(){return {}}}}
  }
  const device={
    limits: {
      maxStorageBufferBindingSize: 1024*1024,
      maxBufferSize: 1024*1024,
      maxComputeWorkgroupsPerDimension: 10000,
      maxComputeInvocationsPerWorkgroup: 256,
      maxTextureDimension2D: 4096,
      ...limits
    },
    createShaderModule({code}){
      return {code,getCompilationInfo: async()=>({messages:compileError&&code.includes('fn noqeri_shade')
        ?[{type:'error',message:'mock WGSL parse failure'}]:[]})}
    },
    createComputePipeline(){return {getBindGroupLayout(){return {}}}},
    createRenderPipeline(descriptor){events.pipelines.push(descriptor);return {getBindGroupLayout(){return {}}}},
    createBuffer({size}){
      const buffer={
        data: new ArrayBuffer(size),mapped:false,destroyed:false,
        async mapAsync(){this.mapped=true},
        getMappedRange(){return this.data},
        unmap(){this.mapped=false},
        destroy(){this.destroyed=true}
      }
      events.buffers.push(buffer)
      return buffer
    },
    createTexture({size,format,usage}){
      const texture={size,format,usage,destroyed:false,createView(){return {}},destroy(){this.destroyed=true}}
      events.textures.push(texture)
      return texture
    },
    createSampler(){return {}},
    createBindGroup(){return {}},
    createCommandEncoder(){
      return {
        beginComputePass(){
          return {setPipeline(){},setBindGroup(){},dispatchWorkgroups(n){events.dispatches.push(n)},end(){}}
        },
        beginRenderPass(){
          return {setPipeline(){},setBindGroup(){},setVertexBuffer(){},draw(count){events.draws.push(count);events.dispatches.push('fragment')},end(){}}
        },
        copyBufferToBuffer(source,so,target,to,bytes){
          new Uint8Array(target.data,to,bytes).set(new Uint8Array(source.data,so,bytes))
        },
        finish(){return {}}
      }
    },
    queue:{
      writeTexture(destination,data,layout,size){events.uploads.push({destination,bytes:data.byteLength,layout,size})},
      writeBuffer(target,offset,typed){
        const bytes = new Uint8Array(typed.buffer,typed.byteOffset,typed.byteLength)
        new Uint8Array(target.data,offset,bytes.length).set(bytes)
      },
      submit(){events.submitted++},
      async onSubmittedWorkDone(){}
    },
    destroy(){events.destroyed++}
  }
  return {
    events,context,device,
    gpu:{getPreferredCanvasFormat(){return 'bgra8unorm'},
      async requestAdapter(){return {limits:device.limits,features:new Set(),async requestDevice(){return device}}}}
  }
}
function computeWgsl(size=64,kind='u32'){
  return 'fn noqeri_shade(nq_arg0: '+kind+') -> '+kind+' {return nq_arg0;}\n'
    +'@group(0) @binding(0) var<storage, read> input_pixels: array<'+kind+'>;\n'
    +'@group(0) @binding(1) var<storage, read_write> output_pixels: array<'+kind+'>;\n'
    +'@compute @workgroup_size('+size+')\nfn main() {}\n'
}
function fragmentWgsl(){
  return 'fn noqeri_shade(nq_arg0: u32, nq_arg1: u32) -> u32 { return nq_arg0; }\n'
    +'@fragment\nfn main() {}\n'
}

test('computed dispatch uses shader workgroup metadata for u32 and float inputs',async()=>{
  for(const [type,workgroup] of [['u32',32],['f32',128],['u32',256]]) {
    const {gpu,events}=gpuMock()
    const Input=type==='u32'?Uint32Array:Float32Array
    const input=new Input(300)
    const output=await runNoqeriCompute(computeWgsl(workgroup,type),input,gpu)
    assert.ok(output instanceof Input)
    assert.equal(output.length,300)
    assert.deepEqual(events.dispatches,[Math.ceil(input.length/workgroup)])
    assert.equal(events.submitted,1)
    assert.ok(events.buffers.every(b=>b.destroyed))
    assert.equal(events.destroyed,1)
  }
})
test('shader element type mismatch is rejected without allocating a device',async()=>{
  const {gpu,events}=gpuMock()
  await assert.rejects(runNoqeriCompute(computeWgsl(64,'f32'),new Uint32Array(2),gpu),
    /type does not match/)
  assert.equal(events.destroyed,0)
})
test('invalid workgroups, hardware limits and WGSL errors release the device',async()=>{
  const {gpu,events}=gpuMock({limits:{maxComputeInvocationsPerWorkgroup:64}})
  await assert.rejects(runNoqeriCompute(computeWgsl(128),new Uint32Array(2),gpu),
    /workgroup limits/)
  assert.equal(events.destroyed,1)
  await assert.rejects(runNoqeriCompute(computeWgsl(512),new Uint32Array(2),gpu),
    /Noqeri compute shader/)
  const bad=gpuMock({compileError:true})
  await assert.rejects(runNoqeriCompute(computeWgsl(),new Uint32Array(2),bad.gpu),
    /WGSL was rejected/)
  assert.equal(bad.events.destroyed,1)
})
test('fragment presenter owns the device until idempotent dispose',async()=>{
  const {gpu,context,events}=gpuMock()
  const release=await renderNoqeriFragment({getContext(){return context}},fragmentWgsl(),gpu)
  assert.equal(events.configured,1)
  assert.equal(events.submitted,1)
  assert.equal(events.destroyed,0)
  release()
  release()
  assert.equal(events.unconfigured,1)
  assert.equal(events.destroyed,1)
})
test('fragment shader errors unconfigure and destroy the partially built context',async()=>{
  const {gpu,context,events}=gpuMock({compileError:true})
  await assert.rejects(
    renderNoqeriFragment({getContext(){return context}},fragmentWgsl(),gpu),/WGSL was rejected/)
  assert.equal(events.configured,1)
  assert.equal(events.unconfigured,1)
  assert.equal(events.destroyed,1)
})

test('vec4 storage dispatch counts vectors rather than component floats',async()=>{
  const {gpu,events}=gpuMock()
  const output=await runNoqeriCompute(computeWgsl(32,'vec4<f32>'),new Float32Array(400),gpu)
  assert.equal(output.length,400)
  assert.deepEqual(events.dispatches,[4])
  assert.equal(events.destroyed,1)
  await assert.rejects(runNoqeriCompute(computeWgsl(32,'vec4<f32>'),
    new Float32Array(3),gpu),/groups of four/)
})
test('textured fragment uploads RGBA8 and disposes GPU resource',async()=>{
  const {gpu,context,events}=gpuMock()
  const shader='fn noqeri_shade(nq_arg0: f32, nq_arg1: f32) -> vec4<f32> { return vec4<f32>(1.0); }\n'
    +'@group(0) @binding(0) var nq_texture: texture_2d<f32>;\n'
    +'@group(0) @binding(1) var nq_sampler: sampler;\n@fragment\nfn main() {}\n'
  const input={width:4,height:2,data:new Uint8Array(32)}
  const release=await renderNoqeriFragment({getContext(){return context}},shader,gpu,input)
  assert.equal(events.textures.length,1)
  assert.equal(events.uploads.length,1)
  assert.equal(events.uploads[0].layout.bytesPerRow,16)
  assert.equal(events.textures[0].destroyed,false)
  release()
  assert.equal(events.textures[0].destroyed,true)
  assert.equal(events.destroyed,1)
})
test('textured shader refuses missing, malformed and over-limit texture without leaking resources',async()=>{
  const mock=gpuMock({limits:{maxTextureDimension2D:2}})
  const shader='fn noqeri_shade(nq_arg0: f32, nq_arg1: f32) -> vec4<f32> { return vec4<f32>(1.0); }\n'
    +'var nq_texture: texture_2d<f32>;\n@fragment\nfn main() {}\n'
  const canvas={getContext(){return mock.context}}
  await assert.rejects(renderNoqeriFragment(canvas,shader,mock.gpu),/Texture must contain/)
  assert.equal(mock.events.destroyed,0)
  await assert.rejects(renderNoqeriFragment(canvas,shader,mock.gpu,
    {width:4,height:1,data:new Uint8Array(16)}),/dimensions exceed/)
  assert.equal(mock.events.destroyed,1)
})

function vertexWgsl(){
  return 'fn noqeri_shade(nq_arg0: vec4<f32>) -> vec4<f32> { return nq_arg0; }\n'
    +'@vertex\nfn main(@location(0) position: vec4<f32>) -> @builtin(position) vec4<f32> { return noqeri_shade(position); }'
}
function meshPixels(){
  return new Float32Array([
    -0.8,-0.8,0.7,1.0, 0.8,-0.8,0.7,1.0, 0.0,0.8,0.7,1.0,
    -0.4,-0.4,0.2,1.0, 0.4,-0.4,0.2,1.0, 0.0,0.4,0.2,1.0
  ])
}
test('3D WebGPU mesh configures depth24plus and cleans up all resources',async()=>{
  const {gpu,context,events}=gpuMock()
  const canvas={width:256,height:192,getContext(){return context}}
  const release=await renderNoqeriMesh3D(canvas,vertexWgsl(),fragmentWgsl(),meshPixels(),gpu)
  assert.equal(events.pipelines.length,1)
  assert.equal(events.pipelines[0].depthStencil.format,'depth24plus')
  assert.equal(events.pipelines[0].depthStencil.depthCompare,'less')
  assert.deepEqual(events.pipelines[0].vertex.buffers[0].attributes,
    [{shaderLocation:0,offset:0,format:'float32x4'}])
  assert.deepEqual(events.draws,[6])
  assert.equal(events.buffers.length,1)
  assert.equal(events.textures.length,1)
  assert.equal(events.submitted,1)
  release();release()
  assert.equal(events.unconfigured,1)
  assert.ok(events.buffers[0].destroyed)
  assert.ok(events.textures[0].destroyed)
  assert.equal(events.destroyed,1)
})
test('3D mesh rejects malformed, non-finite and unsafe clip coordinates before acquiring device',async()=>{
  const {gpu,context,events}=gpuMock()
  const canvas={width:64,height:64,getContext(){return context}}
  for(const v of [
    new Float32Array(8),
    new Float32Array(0),
    new Float32Array([0,0,0,0, 0,1,0,1, 1,0,0,1]),
    new Float32Array([0,0,-0.1,1, 0,1,0,1, 1,0,0,1]),
    new Float32Array([0,0,1,1, 0,1,0,1, Number.NaN,0,0,1])
  ])
    await assert.rejects(renderNoqeriMesh3D(canvas,vertexWgsl(),fragmentWgsl(),v,gpu))
  assert.equal(events.destroyed,0)
})
test('3D mesh rejects exceeded device limits and compilation errors, then frees resources',async()=>{
  const limited=gpuMock({limits:{maxTextureDimension2D:32}})
  await assert.rejects(renderNoqeriMesh3D(
    {width:64,height:64,getContext(){return limited.context}},
    vertexWgsl(),fragmentWgsl(),meshPixels(),limited.gpu),/hardware limits/)
  assert.equal(limited.events.destroyed,1)
  const bad=gpuMock({compileError:true})
  await assert.rejects(renderNoqeriMesh3D(
    {width:64,height:64,getContext(){return bad.context}},
    vertexWgsl(),fragmentWgsl(),meshPixels(),bad.gpu),/WGSL was rejected/)
  assert.equal(bad.events.destroyed,1)
  assert.equal(bad.events.unconfigured,1)
})

test('persistent GPU 3D renderer reuses vertex buffers, pipeline and depth between frames',async()=>{
  const {gpu,context,events}=gpuMock()
  const canvas={width:64,height:64,getContext(){return context}}
  const renderer=await createNoqeriMeshRenderer3D(
    canvas,vertexWgsl(),fragmentWgsl(),gpu,6)
  await renderer.render(meshPixels())
  await renderer.render(meshPixels())
  assert.equal(events.configured,1)
  assert.equal(events.pipelines.length,1)
  assert.equal(events.buffers.length,1)
  assert.equal(events.textures.length,1)
  assert.deepEqual(events.draws,[6,6])
  assert.equal(events.submitted,2)
  const pending=renderer.render(meshPixels())
  await assert.rejects(renderer.render(meshPixels()),/in flight/)
  await pending
  renderer.dispose()
  renderer.dispose()
  assert.equal(events.unconfigured,1)
  assert.ok(events.buffers[0].destroyed && events.textures[0].destroyed)
  assert.equal(events.destroyed,1)
  await assert.rejects(renderer.render(meshPixels()),/disposed/)
})
test('persistent 3D detects canvas resize and oversized mesh; GPU submission errors dispose resources',async()=>{
  const setup=gpuMock()
  const canvas={width:64,height:64,getContext(){return setup.context}}
  const renderer=await createNoqeriMeshRenderer3D(
    canvas,vertexWgsl(),fragmentWgsl(),setup.gpu,3)
  await assert.rejects(renderer.render(meshPixels()),/exceeds persistent capacity/)
  canvas.width=128
  await assert.rejects(renderer.render(meshPixels().slice(0,12)),/resized/)
  renderer.dispose()
  const damaged=gpuMock()
  damaged.device.queue.onSubmittedWorkDone=async()=>{throw new Error('GPU device lost')}
  const bad=await createNoqeriMeshRenderer3D(
    {width:64,height:64,getContext(){return damaged.context}},
    vertexWgsl(),fragmentWgsl(),damaged.gpu,3)
  await assert.rejects(bad.render(meshPixels().slice(0,12)),/device lost/)
  assert.equal(damaged.events.destroyed,1)
  assert.ok(damaged.events.buffers[0].destroyed && damaged.events.textures[0].destroyed)
})

test('WebGPU compatibility inspection reports capabilities without opening a device',async()=>{
  const {gpu,events,device}=gpuMock()
  const base=await inspectNoqeriGpuCompatibility(gpu)
  assert.equal(base.available,true)
  assert.equal(base.limits.maxBufferSize,device.limits.maxBufferSize)
  assert.equal(base.format,'bgra8unorm')
  assert.equal(events.destroyed,0)
  const noGpu=await inspectNoqeriGpuCompatibility({requestAdapter:async()=>null})
  assert.equal(noGpu.available,false)
  let calls=0
  const integrated=await inspectNoqeriGpuCompatibility({
    requestAdapter:async options=>{
      calls++
      return options?.powerPreference ? null : {
        info:{vendor:'mock-integrated'},
        features:new Set(['timestamp-query']),
        limits:{maxTextureDimension2D:2048},
        isFallbackAdapter:false
      }
    },
    getPreferredCanvasFormat:()=> 'rgba8unorm'
  })
  assert.equal(calls,2)
  assert.equal(integrated.adapter.vendor,'mock-integrated')
  assert.deepEqual(integrated.features,['timestamp-query'])
})
test('persistent renderer releases resources and rejects future frames on device loss',async()=>{
  const {gpu,device,context,events}=gpuMock()
  let triggerLoss
  device.lost=new Promise(resolve=>{triggerLoss=resolve})
  const canvas={width:64,height:64,getContext(){return context}}
  const renderer=await createNoqeriMeshRenderer3D(canvas,vertexWgsl(),fragmentWgsl(),gpu,3)
  await renderer.render(meshPixels().slice(0,12))
  triggerLoss({reason:'unknown',message:'mock device removed'})
  await device.lost
  await Promise.resolve()
  assert.equal(events.destroyed,1)
  assert.equal(events.unconfigured,1)
  assert.ok(events.buffers[0].destroyed)
  assert.ok(events.textures[0].destroyed)
  await assert.rejects(renderer.render(meshPixels().slice(0,12)),/mock device removed/)
})

test('WebGPU adapter inspection recovers when preferred adapter request throws',async()=>{
  const {gpu,events}=gpuMock()
  const original=gpu.requestAdapter.bind(gpu)
  let count=0
  gpu.requestAdapter=async options=>{
    count++
    if(options?.powerPreference==='high-performance')
      throw new Error('High-performance selection unsupported')
    return original(options)
  }
  const result=await inspectNoqeriGpuCompatibility(gpu)
  assert.equal(result.available,true)
  assert.equal(count,2)
  assert.equal(events.destroyed,0)
})

test('persistent WebGPU 3D resize reuses pipeline and allocates exactly one depth attachment',async()=>{
  const {gpu,context,events}=gpuMock()
  const canvas={width:64,height:64,getContext(){return context}}
  const r=await createNoqeriMeshRenderer3D(canvas,vertexWgsl(),fragmentWgsl(),gpu,3)
  const input=meshPixels().slice(0,12)
  await r.render(input)
  canvas.width=128
  await assert.rejects(r.render(input),/resized/)
  assert.throws(()=>r.resize(128,8192),/device limits/)
  assert.throws(()=>r.resize(64,64),/backing dimensions/)
  r.resize(128,64)
  assert.equal(events.pipelines.length,1)
  assert.equal(events.textures.length,2)
  assert.equal(events.textures[0].destroyed,true)
  assert.equal(events.textures[1].destroyed,false)
  await r.render(input)
  assert.equal(events.draws.length,2)
  r.dispose()
  assert.equal(events.textures[1].destroyed,true)
  assert.equal(events.destroyed,1)
})
