#!/usr/bin/env python3
"""Same-runner C / Go / Noqeri native-AOT evidence; no superiority assertions."""
import hashlib
import json
import os
import platform
import statistics
import subprocess
import time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/"build"/"comparison"
BUILD.mkdir(parents=True,exist_ok=True)
WORKLOADS={
    "modulo":{"source":"Benchmarks/workloads/modulo_ab.noqeri",
              "expected":"239999951\n"},
    "matrix":{"source":"build/comparison/matrix_comparison.noqeri",
              "expected":"29971\n28448\n"},
    "sorting_reshuffled":{"source":"Benchmarks/workloads/sorting_reshuffled.noqeri",
              "expected":"13104300\n"}
}
def invoke(args):
    start=time.perf_counter_ns()
    run=subprocess.run([str(x) for x in args],cwd=ROOT,capture_output=True)
    duration=(time.perf_counter_ns()-start)/1e6
    if run.returncode!=0:
        raise RuntimeError("failed command "+str(args)+": "+run.stderr.decode(errors="replace")[:3000])
    return duration,run.stdout
def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def versions():
    return {
        "cc":invoke(["cc","--version"])[1].decode().splitlines()[0],
        "go":invoke(["go","version"])[1].decode().strip(),
        "node":invoke(["node","--version"])[1].decode().strip()
    }
matrix=(ROOT/"Benchmarks/workloads/matrix.noqeri").read_text()
old="while pass < 20 as usize"
if matrix.count(old)!=1:
    raise RuntimeError("matrix source is not the pinned comparable workload")
(BUILD/"matrix_comparison.noqeri").write_text(
    matrix.replace(old,"while pass < 200 as usize"))
build_ms={}
build_ms["C"]=invoke([
    "cc","-std=c17","-O3","-o",BUILD/"bench-c",
    ROOT/"Benchmarks/comparison/workloads.c"])[0]
build_ms["Go"]=invoke([
    "go","build","-trimpath","-o",BUILD/"bench-go",
    ROOT/"Benchmarks/comparison/workloads.go"])[0]
for name,config in WORKLOADS.items():
    build_ms["Noqeri:"+name]=invoke([
        "node","scripts/native-build.mjs",config["source"],
        BUILD/("bench-noqeri-"+name),"--compiler=build-compare/noqeri",
        "--opt=-O3"])[0]
programs={
    "C":BUILD/"bench-c",
    "Go":BUILD/"bench-go",
}
output={}
for name,config in WORKLOADS.items():
    cmds={
        "C":[programs["C"],name],
        "Go":[programs["Go"],name],
        "Noqeri":[BUILD/("bench-noqeri-"+name)]
    }
    for lang,cmd in cmds.items():
        _,stdout=invoke(cmd)
        if stdout!=config["expected"].encode():
            raise AssertionError(name+" "+lang+" incorrect output: "+repr(stdout))
    for _ in range(3):
        for cmd in cmds.values():invoke(cmd)
    samples={lang:[] for lang in cmds}
    orders=[("C","Go","Noqeri"),("Noqeri","C","Go"),("Go","Noqeri","C")]
    for i in range(25):
        for lang in orders[i%3]:
            elapsed,stdout=invoke(cmds[lang])
            if stdout!=config["expected"].encode():
                raise AssertionError(name+" "+lang+" changed output after warmup")
            samples[lang].append(elapsed)
    measurements={}
    for lang,values in samples.items():
        ordered=sorted(values)
        measurements[lang]={
            "median_ms":statistics.median(values),
            "p95_ms":ordered[23],
            "raw_ms":values,
            "binary_sha256":digest(cmds[lang][0]),
            "binary_bytes":Path(cmds[lang][0]).stat().st_size
        }
    output[name]={"expected_stdout":config["expected"],
                  "implementations":measurements}
cpu_name="unknown"
cpuinfo=Path("/proc/cpuinfo")
if cpuinfo.exists():
    for line in cpuinfo.read_text(errors="replace").splitlines():
        if line.startswith("model name"):
            cpu_name=line.split(":",1)[1].strip()
            break
report={
    "schema":1,
    "evidence":"same-runner-local-native-execution",
    "source_revision":os.getenv("GITHUB_SHA","unidentified"),
    "execution_model":{"Noqeri":"native-aot","C":"native","Go":"native"},
    "self_hosted_compiler":False,
    "cpu":cpu_name,
    "platform":platform.platform(),
    "machine":platform.machine(),
    "python":platform.python_version(),
    "versions":versions(),
    "warmups":3,
    "samples":25,
    "measurement":"wall-clock subprocess launch plus execution; short runtimes include startup overhead",
    "build_ms":build_ms,
    "workloads":output,
    "limitations":"Three deterministic kernels, x86-64 Linux GitHub runner; no universal language performance parity or compiler self-host proof."
}
(BUILD/"crosslang-report.json").write_text(json.dumps(report,indent=2)+"\n")
print(json.dumps(report,indent=2))
