import test from 'node:test'
import assert from 'node:assert/strict'
import {spawnSync} from 'node:child_process'
test('self-host shader proof refuses missing Stage-2 compiler credentials',()=>{
  const result=spawnSync(process.execPath,['scripts/qualify-shader-stage2.mjs'],{
    encoding:'utf8',env:{...process.env,
      NOQERI_STAGE2_SHADER_COMPILER:'',
      NOQERI_STAGE2_SHADER_PROOF:''},timeout:15000
  })
  assert.equal(result.status,1)
  assert.match(result.stderr,/NQR-GPU-STAGE2-NOT-QUALIFIED/)
})
