#!/usr/bin/env node
import { spawnSync } from 'node:child_process'
import { performance } from 'node:perf_hooks'

const options=[
  ['scalar','build-typed/loop-simd-scalar'],
  ['simd','build-typed/loop-simd-vector']
]
const samples={scalar:[],simd:[]}
function run(path){
  const t=performance.now()
  const result=spawnSync(path,[],{encoding:'utf8',timeout:120000})
  const ms=performance.now()-t
  if(result.status!==0||result.stdout!=='10000000\n')
    throw new Error(path+' output mismatch: '+result.stdout+' '+result.stderr)
  return ms
}
for(let i=0;i<3;i++)for(const [,path] of options)run(path)
for(let i=0;i<15;i++){
  for(const [label,path] of (i%2?options.slice().reverse():options))
    samples[label].push(run(path))
}
function median(values){const sorted=[...values].sort((a,b)=>a-b);return sorted[7]}
const baseline=median(samples.scalar),candidate=median(samples.simd)
const evidence={baseline_ms:baseline,vector_ms:candidate,
  speedup:baseline/candidate,raw:samples,
  note:'15 interleaved wall-clock native runs including process startup'}
console.log(JSON.stringify(evidence,null,2))
if(candidate>=baseline*0.97)
  throw new Error('loop SIMD did not deliver at least 3% median improvement')
