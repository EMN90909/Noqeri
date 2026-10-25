#!/usr/bin/env python3
"""Compare actual native Noqeri x86-64 AOT executables: historical vs current.

The workflow checks out the Oct 24 baseline at a pinned commit alongside the
current branch, builds BOTH compiler revisions on ONE Linux runner and creates
equal-input benchmark executables. Correctness always precedes timing.

Execution time includes process startup, excludes compilation. All raw samples,
compiler hashes, executable hashes and native AOT provenance are recorded.
"""
import hashlib
import json
import os
import platform
import random
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OLD = ROOT / ".benchmark-baseline-20261025"
OUTPUT = ROOT / "build-compare" / "results"
BASELINE = "3ffec426112705dc36c39fb12e58eb3f3f0d3edd"
CURRENT_MAIN = "da642d1ea064236d5a33e62b65daedebd22974b6"
COUNTS = {"sorting_stable": 2000, "sorting_reshuffled": 400,
          "checksum": 5000000, "modulo": 5000000, "matrix_accum": 200}
WARMUPS = 3
SAMPLES = 21


def cmd(args, cwd=ROOT, *, timeout=300):
    p = subprocess.run([str(i) for i in args], cwd=cwd,
                       text=True, capture_output=True, timeout=timeout)
    if p.returncode != 0:
        raise RuntimeError(
            f"EXIT {p.returncode}: {' '.join(str(i) for i in args)}\n"
            f"stdout:\n{p.stdout[-12000:]}\nstderr:\n{p.stderr[-12000:]}")
    return p.stdout.strip()


def checksum(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def cpu_info():
    p = Path("/proc/cpuinfo")
    if p.exists():
        for line in p.read_text().splitlines():
            if line.startswith("model name"):
                return line.split(":", 1)[1].strip()
    return platform.processor() or "unknown"


def sorting_stable(rounds):
    source = (ROOT / "Benchmarks/workloads/sorting.noqeri").read_text()
    assert source.count("round < 200 as usize") == 1
    source = source.replace("round < 200 as usize", f"round < {rounds} as usize")
    c = f'''
    #include <stdint.h>
    #include <stdio.h>
    int main(void) {{
      int64_t v[256];
      for (uint64_t i=0; i<256; ++i)
        v[i] = (int64_t)((i * 7919u + 104729u) % 65521u);
      for (uint64_t round=0; round<{rounds}; ++round) {{
        for (uint64_t a=1; a<256; ++a) {{
          int64_t key=v[a]; uint64_t b=a;
          while (b>0 && v[b-1]>key) {{ v[b]=v[b-1]; --b; }}
          v[b]=key;
        }}
      }}
      printf("%lld\\n%lld\\n",(long long)v[0],(long long)v[255]);
      return 0;
    }}
    '''
    go = f'''
    package main
    import "fmt"
    func main() {{
      var v [256]int64
      for i:=uint64(0);i<256;i++ {{v[i]=int64((i*7919+104729)%65521)}}
      for round:=uint64(0);round<{rounds};round++ {{
        for a:=uint64(1);a<256;a++ {{
          key:=v[a]; b:=a
          for b>0 && v[b-1]>key {{v[b]=v[b-1];b--}}
          v[b]=key
        }}
      }}
      fmt.Printf("%d\\n%d\\n",v[0],v[255])
    }}
    '''
    return source, c, go


def sorting_reshuffled(rounds):
    source = (ROOT / "Benchmarks/workloads/sorting_reshuffled.noqeri").read_text()
    assert source.count("round < 200 as usize") == 1
    source = source.replace("round < 200 as usize", f"round < {rounds} as usize")
    c = f'''
    #include <stdint.h>
    #include <stdio.h>
    int main(void) {{
      int64_t v[256]; uint64_t checksum=0;
      for (uint64_t round=0;round<{rounds};++round) {{
        for (uint64_t i=0;i<256;++i)
          v[i]=(int64_t)((i*7919u+(round+1u)*104729u)%65521u);
        for (uint64_t a=1;a<256;++a) {{
          int64_t key=v[a];uint64_t b=a;
          while (b>0&&v[b-1]>key) {{v[b]=v[b-1];--b;}}
          v[b]=key;
        }}
        checksum+=(uint64_t)v[0]+(uint64_t)v[255];
      }}
      printf("%llu\\n",(unsigned long long)checksum);
      return 0;
    }}
    '''
    go = f'''
    package main
    import "fmt"
    func main() {{
      var v [256]int64
      var checksum uint64
      for round:=uint64(0);round<{rounds};round++ {{
        for i:=uint64(0);i<256;i++ {{
          v[i]=int64((i*7919+(round+1)*104729)%65521)
        }}
        for a:=uint64(1);a<256;a++ {{
          key:=v[a];b:=a
          for b>0 && v[b-1]>key {{v[b]=v[b-1];b--}}
          v[b]=key
        }}
        checksum+=uint64(v[0])+uint64(v[255])
      }}
      fmt.Println(checksum)
    }}
    '''
    return source, c, go


def arithmetic_checksum(n):
    source = f'''
    let sum: int = 7
    let i: usize = 0 as usize
    while i < {n} as usize {{
        sum = (sum * 48271 + ((i % 97 as usize) as int)) % 2147483647
        i = i + 1 as usize
    }}
    print(sum)
    '''
    c = f'''
    #include <stdint.h>
    #include <stdio.h>
    int main(void) {{
        int64_t sum=7;
        for (uint64_t i=0;i<{n}u;i++)
            sum=(sum*48271+(int64_t)(i%97u))%2147483647LL;
        printf("%lld\\n",(long long)sum);
        return 0;
    }}
    '''
    go = f'''
    package main
    import "fmt"
    func main() {{
        var sum int64 = 7
        for i:=uint64(0);i<{n};i++ {{
            sum=(sum*48271+int64(i%97))%2147483647
        }}
        fmt.Println(sum)
    }}
    '''
    return source, c, go


def modulo(n):
    original = (ROOT / "Benchmarks/workloads/modulo_ab.noqeri").read_text()
    assert original.count("5000000 as u64") == 1
    source = original.replace("5000000 as u64", f"{n} as u64")
    c = f'''
    #include <stdint.h>
    #include <stdio.h>
    int main(void) {{
        uint64_t acc=0;
        for (uint64_t i=0;i<{n}u;i++)
            acc=(acc+((i*17u+13u)%97u))%1000000007u;
        printf("%llu\\n",(unsigned long long)acc);
    }}
    '''
    go = f'''
    package main
    import "fmt"
    func main() {{
        var acc uint64 = 0
        for i:=uint64(0);i<{n};i++ {{
            acc=(acc+((i*17+13)%97))%1000000007
        }}
        fmt.Println(acc)
    }}
    '''
    return source, c, go


def matrix_accum(passes):
    original=(ROOT/"Benchmarks/workloads/matrix.noqeri").read_text()
    assert original.count("pass < 20 as usize")==1
    assert original.count("c[row * 16 as usize + col] = sum")==1
    source=(original.replace("pass < 20 as usize",f"pass < {passes} as usize")
            .replace("c[row * 16 as usize + col] = sum",
                     "c[row * 16 as usize + col] = c[row * 16 as usize + col] + sum"))
    c=f'''
    #include <stdint.h>
    #include <stdio.h>
    int main(void){{
      int64_t a[256],b[256],c[256];
      for(uint64_t i=0;i<256;i++) {{
        a[i]=(int64_t)((i*17u+3u)%97u);
        b[i]=(int64_t)((i*29u+11u)%89u);
        c[i]=0;
      }}
      for(uint64_t pass=0;pass<{passes};pass++)
        for(uint64_t row=0;row<16;row++)
          for(uint64_t col=0;col<16;col++) {{
            int64_t sum=0;
            for(uint64_t k=0;k<16;k++)
                sum+=a[row*16+k]*b[k*16+col];
            c[row*16+col]=c[row*16+col]+sum;
          }}
      printf("%lld\\n%lld\\n",(long long)c[0],(long long)c[255]);
      return 0;
    }}
    '''
    go=f'''
    package main
    import "fmt"
    func main() {{
      var a,b,c [256]int64
      for i:=uint64(0);i<256;i++ {{
        a[i]=int64((i*17+3)%97)
        b[i]=int64((i*29+11)%89)
        c[i]=0
      }}
      for pass:=uint64(0);pass<{passes};pass++ {{
        for row:=uint64(0);row<16;row++ {{
          for col:=uint64(0);col<16;col++ {{
            var sum int64
            for k:=uint64(0);k<16;k++ {{
              sum+=a[row*16+k]*b[k*16+col]
            }}
            c[row*16+col]+=sum
          }}
        }}
      }}
      fmt.Printf("%d\\n%d\\n",c[0],c[255])
    }}
    '''
    return source,c,go


WORKLOADS={
    "sorting_stable": sorting_stable,
    "sorting_reshuffled": sorting_reshuffled,
    "checksum": arithmetic_checksum,
    "modulo": modulo,
    "matrix_accum": matrix_accum,
}


def native_build(repo, key, source):
    folder=repo/"build-compare"
    folder.mkdir(exist_ok=True)
    input_file=folder/f"{key}.noqeri"
    executable=folder/f"{key}.native"
    input_file.write_text(source)
    start=time.perf_counter_ns()
    cmd(["node","scripts/native-build.mjs",input_file,executable,
         "--compiler=build-compare/noqeri","--cxx=c++","--opt=-O3"],
        cwd=repo,timeout=180)
    build_ms=(time.perf_counter_ns()-start)/1e6
    provenance=json.loads(Path(str(executable)+".native.json").read_text())
    if provenance["execution_model"]!="native-aot" or provenance["self_hosted_compiler"] is not False:
        raise ValueError(f"NO REAL NATIVE AOT: {repo}/{key}")
    if provenance["optimization"]!="-O3" or provenance["target"]!="linux-x86_64-sysv":
        raise ValueError("target/optimization do not match")
    if checksum(executable)!=provenance["artifact_sha256"]:
        raise ValueError("native executable hash mismatch")
    return {"file":str(executable),"build_ms":build_ms,"provenance":provenance,
            "source_sha256":checksum(input_file)}


def reference_build(key,c,go):
    output=ROOT/"build-compare"
    (output/f"{key}.c").write_text(c)
    (output/f"{key}.go").write_text(go)
    native_c=output/f"{key}.c.exec"
    native_go=output/f"{key}.go.exec"
    start=time.perf_counter_ns()
    cmd(["gcc","-O3","-std=c11",output/f"{key}.c","-o",native_c])
    c_build_ms=(time.perf_counter_ns()-start)/1e6
    start=time.perf_counter_ns()
    cmd(["go","build","-o",native_go,output/f"{key}.go"],timeout=180)
    go_build_ms=(time.perf_counter_ns()-start)/1e6
    return {"C_GCC_O3":{"file":str(native_c),"build_ms":c_build_ms,
                         "sha256":checksum(native_c)},
            "Go":{"file":str(native_go),"build_ms":go_build_ms,
                  "sha256":checksum(native_go)}}


def main():
    if not OLD.is_dir():
        raise RuntimeError("pinned historical checkout missing: "+str(OLD))
    if cmd(["git","rev-parse","HEAD"],cwd=OLD)!=BASELINE:
        raise RuntimeError("historical checkout SHA not pinned to baseline")
    compiler_hashes={}
    for label,repo in (("Noqeri_old",OLD),("Noqeri_new",ROOT)):
        compiler=repo/"build-compare/noqeri"
        if not compiler.is_file():
            raise RuntimeError(f"{label} compiler missing")
        compiler_hashes[label]={"sha256":checksum(compiler),
                                "version":cmd([compiler,"--version"],cwd=repo).splitlines()[0]}
    OUTPUT.mkdir(parents=True,exist_ok=True)
    result={"schema":1,"baseline_revision":BASELINE,
            "latest_main_revision":CURRENT_MAIN,
            "tested_branch_revision":cmd(["git","rev-parse","HEAD"]),
            "execution_model":"standalone native x86-64 AOT, not NQRI VM or JS",
            "self_hosted_compiler":False,
            "optimizer":"-O3","compiler_hashes":compiler_hashes,
            "cpu":cpu_info(),"system":platform.platform(),
            "gcc":cmd(["gcc","--version"]).splitlines()[0],
            "go":cmd(["go","version"]),
            "measurement":"whole-process elapsed wall-clock milliseconds",
            "warmups":WARMUPS,"samples_per_binary":SAMPLES,
            "sample_order":"deterministically interleaved per sample to limit drift",
            "workloads":{}}
    for key, make in WORKLOADS.items():
        source,c,go=make(COUNTS[key])
        old=native_build(OLD,key,source)
        new=native_build(ROOT,key,source)
        refs=reference_build(key,c,go)
        group={"Noqeri_old":old,"Noqeri_new":new,**refs}
        outputs={name:cmd([p["file"]]) for name,p in group.items()}
        if len(set(outputs.values()))!=1:
            raise RuntimeError(f"INCORRECT PROGRAM: {key}: {json.dumps(outputs)}")
        expected=outputs["C_GCC_O3"]
        times={name:[] for name in group}
        names=list(group)
        prng=random.Random(20261025)
        for iteration in range(-WARMUPS,SAMPLES):
            order=names[:]
            prng.shuffle(order)
            for name in order:
                t0=time.perf_counter_ns()
                got=cmd([group[name]["file"]],timeout=120)
                dt=(time.perf_counter_ns()-t0)/1e6
                if got!=expected:raise RuntimeError(f"OUTPUT CHANGED: {key}/{name}")
                if iteration>=0:times[name].append(dt)
        medians={}
        for name in names:
            raw=times[name]
            group[name]["timing"]={
                "median_ms":statistics.median(raw),
                "p95_ms":sorted(raw)[19],
                "samples_ms":raw}
            medians[name]=round(statistics.median(raw),5)
        result["workloads"][key]={
            "iterations_or_rounds":COUNTS[key],"expected_stdout":expected,
            "source_sha256":hashlib.sha256(source.encode()).hexdigest(),
            "results":group}
        print("EXECUTION_RESULT "+json.dumps({
            "workload":key,"size":COUNTS[key],
            "medians_ms":medians,
            "new_vs_old_ratio":round(medians["Noqeri_new"]/medians["Noqeri_old"],4),
            "correct":True}),flush=True)
        (OUTPUT/"native-execution-results.json").write_text(json.dumps(result,indent=2)+"\n")
    print("EXECUTION_ALL_PASSED "+str(OUTPUT/"native-execution-results.json"),flush=True)


if __name__=="__main__":
    main()
