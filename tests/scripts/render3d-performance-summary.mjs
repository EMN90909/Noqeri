#!/usr/bin/env node
// Verified paired, same-run native Noqeri and plain C++ reference timings.
// C++ uses independent full-surface edge evaluation; this is not a
// cross-compiler same-algorithm efficiency ranking or a GPU benchmark.
import {readFileSync} from 'node:fs'
const file=process.argv[2]
if(!file)throw Error('usage: node render3d-performance-summary.mjs render3d-raw.csv')
const lines=readFileSync(file,'utf8').trim().split(/\r?\n/)
if(lines.shift()!=='scene,resolution,engine,sample,render_ns')
  throw Error('Bad native render3d CSV header')
const result=new Map()
for(const line of lines){
  const [scene,resolution,engine,index,ns,...extra]=line.split(',')
  if(extra.length || !['flat-z','interpolated-z'].includes(scene) ||
      resolution!=='256x256' || !['noqeri','c'].includes(engine) ||
      !/^(?:\d|[12]\d)$/.test(index) ||
      !Number.isFinite(Number(ns)) || Number(ns)<=0)
    throw Error('Malformed render3d timing row: '+line)
  const key=scene+':'+engine
  if(!result.has(key))result.set(key,new Map())
  if(result.get(key).has(Number(index)))throw Error('Duplicate sample '+key+':'+index)
  result.get(key).set(Number(index),Number(ns))
}
if(result.size!==4||[...result.values()].some(a=>a.size!==30))
  throw Error('Expected 4 groups of 30 complete timing samples')
function stats(series){
  const data=[...series.values()].sort((a,b)=>a-b)
  return {median_ns:(data[14]+data[15])/2,p95_ns:data[28]}
}
const summary={}
for(const scene of ['flat-z','interpolated-z']){
  const native=stats(result.get(scene+':noqeri'))
  const cpp=stats(result.get(scene+':c'))
  summary[scene]={
    noqeri:native,independent_cpp:cpp,
    ratio_noqeri_to_reference_cpp:native.median_ns/cpp.median_ns
  }
}
process.stdout.write(JSON.stringify({
  schema:1,render_backend:'native-cpu-3d-scalar',hardware_gpu_qualified:false,
  comparison:'same-host distinct native Noqeri optimized edge stepping vs C++ independent reference',
  warmups:5,samples_per_engine:30,scenes:summary,
  provenance_note:'No earlier 3D Noqeri baseline exists; this is an absolute performance qualification, not a speedup vs a past 3D version'
},null,2)+'\n')
