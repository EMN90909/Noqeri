#!/usr/bin/env node
import { spawnSync } from 'node:child_process'
import { performance } from 'node:perf_hooks'

const workloads=[
  ['matrix','Benchmarks/workloads/matrix.noqeri','29971\n28448\n'],
  ['sorting_reshuffled','Benchmarks/workloads/sorting_reshuffled.noqeri','13104300\n']
]
const compiler='build-typed/noqeri'
const outcome=[]
function run(command,args=[],env=process.env){
  const t0=performance.now()
  const result=spawnSync(command,args,{env,encoding:'utf8',timeout:120000})
  if(result.error||result.status!==0)
    throw new Error(command+': '+(result.stderr||result.error||result.status))
  return {ms:performance.now()-t0,output:result.stdout}
}
function percentile(samples,fraction){
  const sorted=[...samples].sort((a,b)=>a-b)
  return sorted[Math.ceil(fraction*sorted.length)-1]
}
for(const [name,source,expected] of workloads){
  const variants={}
  for(const slots of [1,3]){
    const output='build-typed/hotlocal-'+name+'-'+slots
    run('node',['scripts/native-build.mjs',source,output,
      '--compiler='+compiler,'--opt=-O3'],
      {...process.env,NOQERI_HOT_LOCAL_SLOTS:String(slots)})
    for(let i=0;i<3;i++){
      const result=run(output)
      if(result.output!==expected)
        throw new Error(name+': slots='+slots+' wrong output: '+JSON.stringify(result.output))
    }
    variants[slots]={output,samples:[]}
  }
  for(let i=0;i<21;i++){
    for(const slots of (i%2===0?[1,3]:[3,1])){
      const result=run(variants[slots].output)
      if(result.output!==expected)
        throw new Error(name+': output changed during benchmark')
      variants[slots].samples.push(result.ms)
    }
  }
  const baseline=percentile(variants[1].samples,0.5)
  const candidate=percentile(variants[3].samples,0.5)
  const row={name,baseline_rbx_ms:baseline,three_local_ms:candidate,
    speedup:baseline/candidate,p95_one:percentile(variants[1].samples,0.95),
    p95_three:percentile(variants[3].samples,0.95)}
  outcome.push(row)
  if(candidate>baseline*1.15)
    throw new Error(name+': >15% median regression; '+JSON.stringify(row))
}
console.log(JSON.stringify({evidence:'same-compiler-native-AOT-21-interleaved-samples',
  workloads:outcome},null,2))
