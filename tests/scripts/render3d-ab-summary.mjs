#!/usr/bin/env node
// Strict per-scene same-host A/B CPU raster comparison.
// Input CSVs are produced with the same compiler and reference program.
import {readFileSync} from 'node:fs'
const [beforeFile,afterFile]=process.argv.slice(2)
if(!beforeFile||!afterFile)throw Error('Expected pinned-baseline and candidate raw CSV')
function load(path){
  const lines=readFileSync(path,'utf8').trim().split(/\r?\n/)
  if(lines.shift()!=='scene,resolution,engine,sample,render_ns')
    throw Error('Invalid render3d timing header')
  const result=new Map()
  for(const line of lines){
    const [scene,resolution,engine,index,ns,...extra]=line.split(',')
    if(extra.length||!['flat-z','interpolated-z','depth-clear'].includes(scene)||
       resolution!=='256x256'||!['noqeri','c'].includes(engine)||
       !/^(?:\d|[12]\d)$/.test(index)||!Number.isFinite(Number(ns))||Number(ns)<=0)
      throw Error('Malformed timing sample: '+line)
    const key=scene+':'+engine
    if(!result.has(key))result.set(key,new Map())
    if(result.get(key).has(Number(index)))throw Error('Duplicate sample')
    result.get(key).set(Number(index),Number(ns))
  }
  if(result.size!==6||[...result.values()].some(x=>x.size!==30))
    throw Error('Expected 30 valid samples in each of four groups')
  return result
}
const before=load(beforeFile),after=load(afterFile)
const median=values=>{const v=[...values.values()].sort((a,b)=>a-b);return (v[14]+v[15])/2}
const results=[]
for(const scene of ['flat-z','interpolated-z','depth-clear']){
  const old=median(before.get(scene+':noqeri')),now=median(after.get(scene+':noqeri'))
  results.push({scene,baseline_median_ns:old,candidate_median_ns:now,speedup:old/now,
    baseline_cpp_median_ns:median(before.get(scene+':c')),
    candidate_cpp_median_ns:median(after.get(scene+':c'))})
}
const failures=results.filter(x=>x.candidate_median_ns>x.baseline_median_ns*1.25)
console.log(JSON.stringify({schema:1,backend:'native-CPU-3D',pairing:'same host, pinned Noqeri source',
  samples:30,regression_limit_percent:25,passed:failures.length===0,results},null,2))
if(failures.length){
  console.error('Noqeri 3D CPU regression beyond 25%: '+failures.map(x=>x.scene).join(','))
  process.exitCode=1
}
