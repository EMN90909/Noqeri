#!/usr/bin/env python3
"""Benchmark Noqeri's real native CPU graphics, with a matched C reference.

Inside timed section: drawing + seven-pixel frame-observability callback.
Outside timed section: process startup, buffer allocation, PPM encode, JSON.
No physical GPU, browser, or display involved.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import time

ROOT=Path(__file__).resolve().parents[2]
SRC=ROOT/"Benchmarks"/"graphics"

def cmd(args, timeout=120):
    r=subprocess.run(list(map(str,args)),cwd=ROOT,text=True,capture_output=True,timeout=timeout)
    if r.returncode:
        raise RuntimeError(f"COMMAND FAILED {args!r} rc={r.returncode}\n{r.stdout[-4000:]}\n{r.stderr[-4000:]}")
    return r.stdout.strip()

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def run_case(exe, mode, width, height, frames, capture=None):
    args=[exe,mode,width,height,frames]
    if capture is not None: args.append(capture)
    output=cmd(args,timeout=120)
    data=json.loads(output)
    assert data["workload"]==mode and data["width"]==width and data["height"]==height
    assert data["frames"]==frames and data["frame_markers"]==frames
    assert data["elapsed_ns"]>0
    return data

def summarize(samples, pixels, frames):
    ordered=sorted(samples)
    med=statistics.median(samples)
    return {
      "median_ms_frame":med/1e6/frames,
      "p95_ms_frame":ordered[min(len(ordered)-1,int(len(ordered)*.95))]/1e6/frames,
      "median_mpix_per_s":pixels*frames/(med/1e9)/1e6,
      "headless_fps_equivalent":1e9*frames/med,
      "raw_total_ns":samples,
    }

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--compiler",default="build-graphics/noqeri")
    ap.add_argument("--out",default="build-graphics")
    args=ap.parse_args()
    compiler=ROOT/args.compiler
    out=ROOT/args.out
    out.mkdir(parents=True,exist_ok=True)
    assembly=out/"noqeri_graphics.s"
    noqexe=out/"noqeri_graphics"
    cref=out/"c_graphics"
    cobj=out/"reference.o"
    host=SRC/"benchmark_host.cpp"
    original=SRC/"frame_render.noqeri"
    print("Compiling the actual Noqeri Render2D functions...",flush=True)
    cmd([compiler,"check",original])
    cmd([compiler,"build",original,assembly,"-O3"])
    cmd(["g++","-std=c++17","-O2",assembly,host,"-o",noqexe])
    cmd(["gcc","-std=c11","-O3","-c",SRC/"reference.c","-o",cobj])
    cmd(["g++","-std=c++17","-O2","-DBENCH_C_REFERENCE",host,cobj,"-o",cref])
    check_cases=[]
    for mode in ("gradient","checker","fractal"):
        nppm=out/f"{mode}_noqeri.ppm"
        cppm=out/f"{mode}_reference.ppm"
        n=run_case(noqexe,mode,640,360,1,nppm)
        c=run_case(cref,mode,640,360,1,cppm)
        if n["digest"]!=c["digest"] or nppm.read_bytes()!=cppm.read_bytes():
            raise RuntimeError(f"PIXEL MISMATCH: {mode} Noqeri versus C")
        check_cases.append({"mode":mode,"verified":True,"sha256":sha(nppm),"digest":n["digest"],
                            "capture":nppm.name,"pixel_count":640*360})
        print(f"PIXELS_EQUAL {mode}: sha256={sha(nppm)}",flush=True)
    result={
      "revision":cmd(["git","rev-parse","HEAD"]),
      "compiler_version":cmd([compiler,"--version"]).splitlines()[0],
      "compiler_sha256":sha(compiler),
      "noqeri_binary_sha256":sha(noqexe),
      "c_binary_sha256":sha(cref),
      "noqeri_compiler_provenance":"stage0-cpp-native-backend",
      "execution_model":"native-x86_64-ELF",
      "render_engine":"Lib/std/render2d.noqeri",
      "hardware":"CPU, headless; no WebGPU and no display vsync",
      "cpu_model":next((x.split(":",1)[1].strip() for x in Path("/proc/cpuinfo").read_text().splitlines() if x.startswith("model name")),platform.processor()),
      "os":platform.platform(),
      "gcc":cmd(["gcc","--version"]).splitlines()[0],
      "warmup_count":3,"sample_count":15,
      "correctness":check_cases,
      "workloads":[]
    }
    for mode in ("gradient","checker","fractal"):
        for width,height,frames in (((320,180,2),(640,360,1),(1280,720,1)) if mode=="fractal" else ((320,180,25),(640,360,12),(1280,720,3))):
            # Every frame's output is sampled by a host callback, preventing
            # aggressive compilers from eliminating repeated writes.
            series={"Noqeri":[],"C":[]}
            exp_digest=None
            for trial in range(-3,15):
                order=("Noqeri","C") if trial%2==0 else ("C","Noqeri")
                each={}
                for name in order:
                    app=noqexe if name=="Noqeri" else cref
                    x=run_case(app,mode,width,height,frames)
                    each[name]=x
                    if trial>=0:series[name].append(x["elapsed_ns"])
                if each["Noqeri"]["digest"]!=each["C"]["digest"]:
                    raise RuntimeError(f"Frame output mismatch at {mode} {width}x{height}")
                exp_digest=each["Noqeri"]["digest"]
            stats={name:summarize(ns,width*height,frames) for name,ns in series.items()}
            entry={"mode":mode,"width":width,"height":height,
                   "frames_per_sample":frames,"pixels_per_frame":width*height,
                   "frame_sample_digest":exp_digest,"results":stats}
            result["workloads"].append(entry)
            print("GRAPHICS_RESULT "+json.dumps({
                "mode":mode,"resolution":f"{width}x{height}","frames":frames,
                "noqeri_ms_frame":round(stats["Noqeri"]["median_ms_frame"],5),
                "c_ms_frame":round(stats["C"]["median_ms_frame"],5),
                "ratio":round(stats["Noqeri"]["median_ms_frame"]/stats["C"]["median_ms_frame"],2),
                "noqeri_mpix_s":round(stats["Noqeri"]["median_mpix_per_s"],2)
            }),flush=True)
    target=out/"graphics_results.json"
    target.write_text(json.dumps(result,indent=2)+"\n")
    print("GRAPHICS_REPORT",target,flush=True)

if __name__=="__main__":
    main()
