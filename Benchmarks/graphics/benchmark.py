#!/usr/bin/env python3
"""Measured Noqeri CPU raster + WGSL compiler benchmark, no synthetic Noqeri timings.

Run on Linux x86-64 from the repository root:
  cmake -S . -B build-graphics -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
  cmake --build build-graphics --target noqeri -j 2
  python3 Benchmarks/graphics/benchmark.py --compiler build-graphics/noqeri

The generated Noqeri source imports the real Lib/std/render2d.noqeri. The
Noqeri and C raster programs link the SAME C++ observation/timer host and
produce independently compared PPMs. Shader compilation only measures WGSL
generation and Naga validation; it does not execute a physical GPU.
"""
import argparse
import hashlib
import json
import os
import platform
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ROOT / "Benchmarks/graphics/generated"
OUT = ROOT / "build-graphics/benchmark"
SCENES = [
    ("gradient", 64, 64, 50),
    ("gradient", 256, 256, 20),
    ("gradient", 512, 512, 8),
    ("checker", 256, 256, 20),
]
SHADERS = [
    ("color_gradient", "fragment", []),
    ("grayscale_fragment", "fragment", []),
    ("posterize_fragment", "fragment", []),
    ("invert_compute", "compute", []),
    ("float_compute", "compute", ["--workgroup-size=128"]),
]


def execute(args, *, env=None, cwd=ROOT):
    p = subprocess.run([str(v) for v in args], cwd=cwd, env=env, capture_output=True, text=True)
    if p.returncode:
        raise RuntimeError(
            f"FAILED: {' '.join(map(str, args))}\nexit: {p.returncode}\n"
            f"stdout:\n{p.stdout[-12000:]}\nstderr:\n{p.stderr[-12000:]}"
        )
    return p.stdout.strip()


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def render_source(mode, width, height, frames):
    count = width * height
    raster = (
        f"nqRenderGradient(surface, {width} as usize, {height} as usize)"
        if mode == "gradient"
        else (
            f"nqRenderChecker(surface, {width} as usize, {height} as usize,"
            f" 16 as usize, 4278255360 as u32, 4294901760 as u32)"
        )
    )
    return f'''import "../../../Lib/std/render2d.noqeri"

extern function nqGraphicsObserve(pixels: *u32, count: usize): int

let framebuffer: [u32; {count}]
let surface: []u32 = slice(framebuffer)
let frame: usize = 0 as usize
while frame < {frames} as usize {{
    if {raster} {{
        nqGraphicsObserve(&framebuffer[0], {count} as usize)
    }}
    frame = frame + 1 as usize
}}
'''


def c_reference(mode, width, height, frames):
    count = width * height
    if mode == "gradient":
        pixel = f'''size_t x = i % {width}, y = i / {width};
            uint32_t r = {width}>1 ? (uint32_t)((x * 255) / ({width}-1)) : 0;
            uint32_t g = {height}>1 ? (uint32_t)((y * 255) / ({height}-1)) : 0;
            pixels[i] = 0xff000040u + (r << 16) + (g << 8);'''
    else:
        pixel = f'''size_t x = i % {width}, y = i / {width};
            pixels[i] = (((x / 16) + (y / 16)) % 2 == 0)
                ? 0xff00ff00u : 0xffff0000u;'''
    return f'''#include <stdint.h>
#include <stddef.h>
extern int nqGraphicsObserve(uint32_t* pixels, size_t count);
int noqeri_entry(const void *abi) {{
    (void)abi;
    uint32_t pixels[{count}];
    for (size_t frame=0; frame<{frames}; ++frame) {{
        for (size_t i=0; i<{count}; ++i) {{
            {pixel}
        }}
        if (nqGraphicsObserve(pixels, {count}) != 0) return 1;
    }}
    return 0;
}}
'''


HOST = r'''#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#ifndef GRAPHICS_W
#error "Width missing"
#endif
extern "C" int noqeri_entry(const void*);
static volatile uint64_t g_sink = 0;
static int g_observations = 0;
static bool g_capture_complete = false;
static bool g_invalid = false;

extern "C" int nqGraphicsObserve(uint32_t* pixels, size_t count) {
    if (!pixels || count != size_t(GRAPHICS_W) * size_t(GRAPHICS_H)) {
        g_invalid=true;
        return 1;
    }
    const size_t ids[] = {0, count/3, count/2, count*3/4, count-1};
    uint64_t sample=0;
    for (auto id : ids) sample = sample * uint64_t(1315423911u) + pixels[id];
    g_sink = g_sink ^ sample;
    ++g_observations;
    const char* output = std::getenv("GRAPHICS_CAPTURE");
    if (output && *output && !g_capture_complete) {
        std::ofstream f(output, std::ios::binary);
        if (!f) {g_invalid=true;return 2;}
        f << "P6\n" << GRAPHICS_W << " " << GRAPHICS_H << "\n255\n";
        for (size_t i=0;i<count;++i) {
            const uint32_t v=pixels[i];
            const char color[3] = {
                char((v>>16)&255),char((v>>8)&255),char(v&255)
            };
            f.write(color,3);
        }
        f.close();
        if (!f) {g_invalid=true;return 3;}
        g_capture_complete=true;
    }
    return 0;
}

int main() {
    std::vector<double> times;
    times.reserve(15);
    uint64_t last_probe=0;
    for (int run=-3; run<15; ++run) {
        g_sink=0;
        g_observations=0;
        g_invalid=false;
        auto start=std::chrono::steady_clock::now();
        int rc=noqeri_entry(nullptr);
        auto end=std::chrono::steady_clock::now();
        if(rc!=0 || g_invalid || g_observations!=GRAPHICS_FRAMES) {
            std::cerr<<"render failed rc="<<rc
                     <<" observations="<<g_observations<<"\n";
            return 3;
        }
        last_probe = g_sink;
        if (run>=0) {
            double ms=std::chrono::duration<double,std::milli>(end-start).count();
            times.push_back(ms / GRAPHICS_FRAMES);
        }
    }
    std::sort(times.begin(),times.end());
    std::cout<<std::fixed<<std::setprecision(9)
             <<"{\"median_ms_per_frame\":"<<times[7]
             <<",\"p95_ms_per_frame\":"<<times[14]
             <<",\"min_ms_per_frame\":"<<times[0]
             <<",\"max_ms_per_frame\":"<<times.back()
             <<",\"observed_frames\":"<<GRAPHICS_FRAMES
             <<",\"pixel_probe\":"<<last_probe
             <<",\"samples_per_frame_ms\":[";
    for (size_t i=0;i<times.size();++i) {
        if(i)std::cout<<",";
        std::cout<<times[i];
    }
    std::cout<<"]}\n";
    return 0;
}
'''


def gpu_nodes():
    dri = Path("/dev/dri")
    return sorted(p.name for p in dri.iterdir()) if dri.exists() else []


def benchmark_rasters(compiler):
    results=[]
    (OUT/"host.cpp").write_text(HOST)
    for mode,w,h,frames in SCENES:
        key=f"{mode}-{w}x{h}"
        source=SOURCES/f"{key}.noqeri"
        source.write_text(render_source(mode,w,h,frames))
        csource=OUT/f"{key}.c"
        csource.write_text(c_reference(mode,w,h,frames))
        assembly=OUT/f"{key}.s"
        execute([compiler,"check",source])
        execute([compiler,"build",source,assembly,"-O3"])
        cobj=OUT/f"{key}-reference.o"
        execute(["gcc","-O3","-std=c11","-c",csource,"-o",cobj])
        binaries={}
        for label,object_source in (("noqeri",assembly),("c",cobj)):
            binary=OUT/f"{key}-{label}"
            execute(["g++","-std=c++17","-O2","-DGRAPHICS_W="+str(w),
                     "-DGRAPHICS_H="+str(h),"-DGRAPHICS_FRAMES="+str(frames),
                     OUT/"host.cpp",object_source,"-o",binary])
            capture=OUT/f"{key}-{label}.ppm"
            env=os.environ.copy()
            env["GRAPHICS_CAPTURE"]=str(capture)
            observed=json.loads(execute([binary],env=env).splitlines()[-1])
            if observed["observed_frames"]!=frames or observed["median_ms_per_frame"]<=0:
                raise RuntimeError("Incomplete frames or timing")
            binaries[label]={"timing":observed,"capture_sha256":digest(capture),
                             "binary_sha256":digest(binary),"binary_bytes":binary.stat().st_size}
        if binaries["noqeri"]["capture_sha256"]!=binaries["c"]["capture_sha256"]:
            raise RuntimeError(f"Noqeri and C render differently: {key}")
        if binaries["noqeri"]["timing"]["pixel_probe"]!=binaries["c"]["timing"]["pixel_probe"]:
            raise RuntimeError(f"Noqeri/C pixel probe mismatch: {key}")
        n=binaries["noqeri"]["timing"]["median_ms_per_frame"]
        c=binaries["c"]["timing"]["median_ms_per_frame"]
        row={"scene":mode,"width":w,"height":h,"frames_per_sample":frames,
             "pixels_per_frame":w*h,"noqeri_ms_per_frame":n,"c_ms_per_frame":c,
             "noqeri_fps_equivalent":1000/n,"c_fps_equivalent":1000/c,
             "noqeri_over_c_ratio":n/c,
             "verified_equal_ppm_sha256":binaries["noqeri"]["capture_sha256"],
             "source_sha256":digest(source),"assembly_sha256":digest(assembly),
             "binaries":binaries}
        results.append(row)
        print("RASTER_RESULT "+json.dumps(
            {k:row[k] for k in ("scene","width","height","frames_per_sample",
              "noqeri_ms_per_frame","c_ms_per_frame","noqeri_over_c_ratio",
              "verified_equal_ppm_sha256")}),flush=True)
    return results


def benchmark_shaders(compiler, naga=True):
    results=[]
    output_paths=[]
    for name,stage,flags in SHADERS:
        src=ROOT/f"examples/shaders/{name}.noqeri"
        output=OUT/f"{name}.wgsl"
        times=[]
        expected=None
        for i in range(-3,15):
            start=time.perf_counter_ns()
            execute([compiler,"shader",src,output,f"--stage={stage}",*flags])
            elapsed_ms=(time.perf_counter_ns()-start)/1e6
            sha=digest(output)
            if expected is not None and sha!=expected:
                raise RuntimeError(f"Shader generation not deterministic: {name}")
            expected=sha
            if i>=0: times.append(elapsed_ms)
        output_paths.append(output)
        row={"name":name,"stage":stage,"median_compilation_ms":statistics.median(times),
             "p95_compilation_ms":sorted(times)[14],"raw_ms":times,
             "wgsl_sha256":expected,"wgsl_bytes":output.stat().st_size}
        results.append(row)
        print("SHADER_RESULT "+json.dumps(
            {k:row[k] for k in ("name","stage","median_compilation_ms","wgsl_bytes")}),flush=True)
    validation="unavailable"
    if naga:
        execute(["cargo","run","--quiet","--manifest-path",
                 ROOT/"tests/wgsl-validator/Cargo.toml","--",*output_paths])
        validation="passed"
    return results,validation


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--compiler",default="build-graphics/noqeri")
    parser.add_argument("--skip-naga",action="store_true")
    args=parser.parse_args()
    compiler=Path(args.compiler)
    if not compiler.is_absolute():
        compiler=ROOT/compiler
    if not compiler.exists():
        parser.error("Noqeri compiler missing. Build CMake target noqeri first.")
    SOURCES.mkdir(parents=True,exist_ok=True)
    OUT.mkdir(parents=True,exist_ok=True)
    result={"schema":1,"git_revision":execute(["git","rev-parse","HEAD"]),
            "compiler":execute([compiler,"--version"]).splitlines()[0],
            "compiler_sha256":digest(compiler),
            "host":platform.platform(),"machine":platform.machine(),
            "graphics_hardware_nodes":gpu_nodes(),
            "native_compiler":"C++ Stage-0 Noqeri native x86-64 assembly, g++ linked",
            "runtime_measurement":"in-process steady_clock; 3 warmups + 15 samples",
            "includes_process_startup":False,"pixel_correctness":"byte-identical P6 PPM against C",
            "gpu_execution_measured":False}
    result["cpu_rasters"]=benchmark_rasters(compiler)
    result["shader_compiler"],result["naga_validation"]=benchmark_shaders(
        compiler,naga=not args.skip_naga)
    path=OUT/"graphics-results.json"
    path.write_text(json.dumps(result,indent=2)+"\n")
    print("GRAPHICS_FINAL_RESULTS "+str(path),flush=True)


if __name__=="__main__":
    main()
