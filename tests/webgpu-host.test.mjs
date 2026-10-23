import test from 'node:test'
import assert from 'node:assert/strict'
import {runNoqeriCompute, renderNoqeriFragment} from '../examples/shaders/webgpu_host.mjs'

globalThis.GPUBufferUsage = {STORAGE: 1, COPY_DST: 2, COPY_SRC: 4, MAP_READ: 8}
globalThis.GPUMapMode = {READ: 1}

function gpuMock({compileError = false, limits = {}} = {}) {
  const events={dispatches:[],buffers:[],destroyed:0,configured:0,unconfigured:0,submitted:0}
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
      ...limits
    },
    createShaderModule({code}){
      return {code,getCompilationInfo: async()=>({messages:compileError&&code.includes('fn noqeri_shade')
        ?[{type:'error',message:'mock WGSL parse failure'}]:[]})}
    },
    createComputePipeline(){return {getBindGroupLayout(){return {}}}},
    createRenderPipeline(){return {}},
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
    createBindGroup(){return {}},
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
