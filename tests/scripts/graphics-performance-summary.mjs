#!/usr/bin/env node
import {readFileSync} from 'node:fs'
const [baselinePath,optimizedPath]=process.argv.slice(2)
if(!baselinePath||!optimizedPath)throw Error('baseline.csv and optimized.csv required')
function load(path){
  const out=new Map()
  const lines=readFileSync(path,'utf8').trim().split(/\r?\n/)
  if(lines.shift()!=='scene,resolution,engine,sample,render_ns')
    throw Error('Invalid raw raster CSV schema: '+path)
  for(const line of lines) {
    const parts=line.split(',')
    if(parts.length!==5)throw Error('Malformed raw benchmark row')
    const [scene,resolution,engine,sample,ns]=parts
    if(!['gradient','checker','clear'].includes(scene)||!['noqeri','c'].includes(engine)||
       !/^\d+x\d+$/.test(resolution) ||
       !/^(?:\d|[12]\d)$/.test(sample) ||
       !Number.isFinite(Number(ns))||Number(ns)<0)
      throw Error('Invalid benchmark row')
    const key=[scene,resolution,engine].join(':')
    if(!out.has(key))out.set(key,[])
    out.get(key).push(Number(ns))
  }
  if(out.size!==18 || [...out.values()].some(v=>v.length!==30))
    throw Error('Require 30 samples for all 3 scenes x 3 dimensions x 2 engines')
  return out
}
function stats(data){
  const sorted=[...data].sort((a,b)=>a-b)
  return {median_ns:(sorted[14]+sorted[15])/2,p95_ns:sorted[28]}
}
const baseline=load(baselinePath),optimized=load(optimizedPath)
const scenes=[]
const regressionFailures=[]
for(const [key,b] of baseline) {
  const o=optimized.get(key)
  if(!o)throw Error('Missing paired measurement: '+key)
  const before=stats(b),after=stats(o)
  const [scene,resolution,engine]=key.split(':')
  if(engine==='noqeri' && resolution!=='64x64' &&
     after.median_ns>before.median_ns*1.20){
    regressionFailures.push(key)
  }
  const cKey=scene+':'+resolution+':c'
  const optimizedC=optimized.get(cKey)
  scenes.push({key,baseline_median_ns:before.median_ns,optimized_median_ns:after.median_ns,
    optimized_p95_ns:after.p95_ns,
    speedup_vs_baseline:after.median_ns>0?before.median_ns/after.median_ns:null,
    optimized_noqeri_vs_c_ratio:engine==='noqeri' && optimizedC?
      after.median_ns/stats(optimizedC).median_ns:null})
}
const result={
  schema:2,execution:'native-cpu-raster-only',not_gpu_hardware:true,
  sample_count_per_scene_engine:30,
  comparison:'same CI host, separately executed baseline and optimized binaries',
  regression_gate:{max_slowdown_percent:20,resolutions:['256x256','512x512'],
    passed:regressionFailures.length===0,failures:regressionFailures},
  results:scenes,
  claim:'CI metrics are not comparable to uploaded CSV without matched hardware/provenance'
}
process.stdout.write(JSON.stringify(result,null,2)+'\n')
if(regressionFailures.length){
  console.error('Native CPU raster regression against pinned same-run baseline: '+regressionFailures.join(', '))
  process.exitCode=1
}
