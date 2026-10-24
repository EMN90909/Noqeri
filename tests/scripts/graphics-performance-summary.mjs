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
function median(data){const v=[...data].sort((a,b)=>a-b);return (v[14]+v[15])/2}
const baseline=load(baselinePath),optimized=load(optimizedPath)
const scenes=[]
for(const [key,b] of baseline) {
  const o=optimized.get(key)
  if(!o)throw Error('Missing paired measurement: '+key)
  const before=median(b),after=median(o)
  scenes.push({key,baseline_median_ns:before,optimized_median_ns:after,
    speedup_vs_baseline:after>0?before/after:null})
}
process.stdout.write(JSON.stringify({
  schema:1,execution:'native-cpu-raster-only',not_gpu_hardware:true,
  sample_count_per_scene_engine:30,comparison:'paired CI host, separate executables',
  results:scenes,
  claim:'CI metrics are not comparable to uploaded CSV without matched hardware/provenance'
},null,2)+'\n')
