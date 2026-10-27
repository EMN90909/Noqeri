// Repeatable build-throughput benchmark for the experimental Node-hosted DSL.
// Compare the same deterministic assets with serial and bounded parallel IO.
import assert from 'node:assert/strict';
import {mkdtemp,mkdir,writeFile,readFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {performance} from 'node:perf_hooks';
import {buildWebProject} from '../Runtime/web-project.mjs';

const samples=Number(process.env.NOQERI_BUILD_BENCH_SAMPLES||4);
const assetCount=Number(process.env.NOQERI_BUILD_BENCH_ASSETS||160);
const budget=Number(process.env.NOQERI_BUILD_BENCH_P95_MS||3000);
assert.ok(Number.isInteger(samples)&&samples>=2&&samples<=12);
assert.ok(Number.isInteger(assetCount)&&assetCount>=20&&assetCount<=1000);
assert.ok(Number.isFinite(budget)&&budget>0);
const scratch=await mkdtemp(join(tmpdir(),'noqeri-web-perf-'));
const previous=process.env.NOQERI_WEB_BUILD_CONCURRENCY;
function percentile(values,p) {
  const sorted=values.toSorted((a,b)=>a-b);
  return Number(sorted[Math.ceil(sorted.length*p)-1].toFixed(2));
}
try {
  const src=join(scratch,'src');
  await mkdir(src);
  await writeFile(join(src,'main.noqeri'),'route "/" { main { h1 { "Noqeri pre-render" } } }\nroute "/about" { h1 { "About" } }\n');
  for(let i=0;i<assetCount;i++)
    await writeFile(join(src,'asset-'+String(i).padStart(4,'0')+'.css'),
      '/* static asset */\n'+'.benchmark-'+i+'{padding:4px}\n'.repeat(25));
  const runs={'1':[],'8':[]};
  let iteration=0;
  for(let i=0;i<samples+1;i++)for(const parallelism of (i%2===0?['1','8']:['8','1'])) {
    process.env.NOQERI_WEB_BUILD_CONCURRENCY=parallelism;
    const out=join(scratch,'dist-'+(iteration++));
    const started=performance.now();
    await buildWebProject({source:src,out});
    const duration=performance.now()-started;
    assert.match(await readFile(join(out,'index.html'),'utf8'),/Noqeri pre-render/);
    assert.match(await readFile(join(out,'about/index.html'),'utf8'),/About/);
    if(i!==0)runs[parallelism].push(duration);
  }
  const sequential={p50_ms:percentile(runs['1'],.5),p95_ms:percentile(runs['1'],.95)};
  const parallel={p50_ms:percentile(runs['8'],.5),p95_ms:percentile(runs['8'],.95)};
  const report={benchmark:'web-view-build-assets',samples_per_mode:samples,
    assets:assetCount,node_build_host:true,sequential,parallel,
    observed_median_ratio:Number((sequential.p50_ms/parallel.p50_ms).toFixed(2))};
  console.log('NOQERI_BUILD_PERFORMANCE',JSON.stringify(report));
  assert.ok(sequential.p95_ms<budget && parallel.p95_ms<budget,
    'Noqeri web build p95 exceeded '+budget+'ms');
} finally {
  if(previous===undefined)delete process.env.NOQERI_WEB_BUILD_CONCURRENCY;
  else process.env.NOQERI_WEB_BUILD_CONCURRENCY=previous;
  await rm(scratch,{recursive:true,force:true});
}
