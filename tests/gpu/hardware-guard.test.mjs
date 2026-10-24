import test from 'node:test'
import assert from 'node:assert/strict'
import {readFile} from 'node:fs/promises'

async function source(path){
  return readFile(new URL('../../'+path,import.meta.url),'utf8')
}
test('physical GPU acceptance is explicitly manual and never uses software fallback',async()=>{
  const workflow=await source('.github/workflows/gpu-physical-qualification.yml')
  assert.match(workflow,/workflow_dispatch:/)
  assert.match(workflow,/runs-on: \[self-hosted, physical-gpu\]/)
  assert.doesNotMatch(workflow,/runs-on: ubuntu-latest/)
  const html=await source('tests/gpu/qualify.html')
  assert.match(html,/hardware_qualification\.mjs/)
  const browser=await source('tests/gpu/hardware_qualification.mjs')
  assert.match(browser,/forceFallbackAdapter:false/)
  assert.match(browser,/isFallbackAdapter===true/)
  assert.match(browser,/expectedVendor/)
  assert.match(browser,/integer-compute-readback/)
  assert.match(browser,/float-compute-readback/)
  assert.match(browser,/vec4-compute-readback/)
  assert.match(browser,/texture-upload-and-fragment-submit/)
  assert.match(browser,/device-loss-lifecycle/)
  const host=await source('tests/gpu/run-qualification.mjs')
  assert.match(host,/headless:false/)
  assert.match(host,/gpu-evidence.json/)
  assert.doesNotMatch(host,/--use-angle=swiftshader|--use-gl=swiftshader/)
})
