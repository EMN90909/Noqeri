import test from 'node:test'
import assert from 'node:assert/strict'
import {runNoqeriCompute, renderNoqeriFragment} from '../examples/shaders/webgpu_host.mjs'

globalThis.GPUBufferUsage = {STORAGE: 1, COPY_DST: 2, COPY_SRC: 4, MAP_READ: 8}
globalThis.GPUMapMode = {READ: 1}
globalThis.GPUTextureUsage = {TEXTURE_BINDING: 16, COPY_DST: 32}

function gpuMock({compileError = false, limits = {}} = {}) {
  const events={dispatches:[],buffers:[],textures:[],textureWrites:[],bindGroups:[],destroyed:0,configured:0,unconfigured:0,submitted:0}
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
    createRenderPipeline(){return {getBindGroupLayout(){return {}}}},
    createTexture({size,format,usage}){
      const texture={size,format,usage,destroyed:false,
        createView(){return {texture}},
        destroy(){this.destroyed=true}
      }
      events.textures.push(texture)
      return texture
    },
    createSampler(){return {kind:'sampler'}},
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
    createBindGroup(info){events.bindGroups.push(info);return {}},
    createCommandEncoder(){
      return {
        beginComputePass(){
          return {setPipeline(){},setBindGroup(){},dispatchWorkgroups(n){events.dispatches.push(n)},end(){}}
        },
        beginRenderPass(){
          return {setPipeline(){},draw(){events.dispatches.push('fragment')},end(){}}
        },
        copyBufferToBuffer(source,so,target,to,bytes){
          new Uint8Array(target.data,to,bytes).set(new Uint8Array(source.data,so,bytes))
        },
        finish(){return {}}
      }
    },
    queue:{
      writeTexture(destination,source,layout,size){
        events.textureWrites.push({destination,source,layout,size})
      },
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
      async requestAdapter(){return {async requestDevice(){return device}}}}
  }
}
function computeWgsl(size=64,kind='u32'){
  return 'fn noqeri_shade(nq_arg0: '+kind+') -> '+kind+' {return nq_arg0;}\n'
    +'@group(0) @binding(0) var<storage, read> input_pixels: array<'+kind+'>;\n'
    +'@group(0) @binding(1) var<storage, read_write> output_pixels: array<'+kind+'>;\n'
    +'@compute @workgroup_size('+size+')\nfn main() {}\n'
}
function textureWgsl(){
  return 'fn noqeri_shade(nq_arg0: f32, nq_arg1: f32) -> vec4<f32> {'
    +' return textureSample(nq_texture,nq_sampler,vec2<f32>(0.5,0.5)); }\n'
    +'@group(0) @binding(0) var nq_texture: texture_2d<f32>;\n'
    +'@group(0) @binding(1) var nq_sampler: sampler;\n@fragment\nfn main() {}\n'
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

test('vec4 compute uses packed four-float elements, not scalar lengths, when dispatching',async()=>{
  const {gpu,events}=gpuMock()
  const input=new Float32Array(256*4)
  const output=await runNoqeriCompute(computeWgsl(64,'vec4<f32>'),input,gpu)
  assert.ok(output instanceof Float32Array)
  assert.equal(output.length,input.length)
  assert.deepEqual(events.dispatches,[4])
  assert.ok(events.buffers.every(b=>b.destroyed))
  await assert.rejects(
    runNoqeriCompute(computeWgsl(64,'vec4<f32>'),new Float32Array(5),gpu),
    /four Float32 elements/)
})
test('texture shader uploads padded RGBA8 and binds sampler/texture with safe cleanup',async()=>{
  const {gpu,context,events}=gpuMock()
  const image=new Uint8Array([255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255])
  const release=await renderNoqeriFragment(
    {getContext(){return context}},textureWgsl(),gpu,
    {pixels:image,width:2,height:2})
  assert.equal(events.textures.length,1)
  assert.equal(events.textureWrites.length,1)
  assert.equal(events.textureWrites[0].layout.bytesPerRow,256)
  assert.equal(events.textureWrites[0].source.byteLength,512)
  assert.equal(events.bindGroups.length,1)
  assert.equal(events.bindGroups[0].entries.length,2)
  assert.equal(events.textures[0].destroyed,false)
  release()
  assert.equal(events.textures[0].destroyed,true)
  assert.equal(events.destroyed,1)
})
test('invalid/missing textures reject and always release graphics device',async()=>{
  const {gpu,context,events}=gpuMock()
  const canvas={getContext(){return context}}
  await assert.rejects(renderNoqeriFragment(canvas,textureWgsl(),gpu),
    /requires an RGBA8 texture input/)
  await assert.rejects(
    renderNoqeriFragment(canvas,textureWgsl(),gpu,
      {pixels:new Uint8Array(3),width:2,height:2}),
    /RGBA8 Uint8Array/)
  assert.equal(events.destroyed,2)
  assert.equal(events.unconfigured,2)
  assert.equal(events.textures.length,0)
})
