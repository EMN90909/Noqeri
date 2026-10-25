#!/usr/bin/env python3
"""Compile Noqeri 2D/3D GPU sources and write independent direct-WGSL references.

Only the Noqeri compiler generates files in "noqeri". "reference" WGSL is
separately authored. Naga syntax checks run on all generated Noqeri WGSL.
The native Vulkan software adapter renderer validates true pixel readbacks.
"""
import hashlib
import json
import pathlib
import statistics
import subprocess
import sys
import time

ROOT=pathlib.Path(__file__).resolve().parents[2]
OUTPUT=ROOT/"build-gpu-wgsl"
NQ=OUTPUT/"noqeri"
REF=OUTPUT/"reference"
SOURCE=OUTPUT/"sources"
SHADES=[
    ("color_gradient","examples/shaders/color_gradient.noqeri","fragment",[]),
    ("posterize_fragment","examples/shaders/posterize_fragment.noqeri","fragment",[]),
    ("vector_fragment","examples/shaders/vector_fragment.noqeri","fragment",[]),
    ("vertex3d","examples/shaders/vertex3d.noqeri","vertex",[]),
    ("fragment3d","examples/shaders/fragment3d.noqeri","fragment",[]),
]
FACES=[
    ("face0",(0.96,0.28,0.39,1.0)),
    ("face1",(0.20,0.82,0.95,1.0)),
    ("face2",(0.28,0.35,0.98,1.0)),
    ("face3",(0.99,0.76,0.20,1.0)),
    ("face4",(0.24,0.93,0.62,1.0)),
    ("face5",(0.78,0.37,0.94,1.0)),
]
NEAR=(0.98,0.22,0.24,1.0)

def call(args,timeout=120):
    p=subprocess.run([str(x) for x in args],cwd=ROOT,capture_output=True,text=True,timeout=timeout)
    if p.returncode:
        raise RuntimeError(f"Command failed ({p.returncode}): {args}\n{p.stdout}\n{p.stderr}")
    return p.stdout

def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()

def wgsl_fragment(color):
    return """
@fragment
fn main(@builtin(position) pos: vec4<f32>) -> @location(0) vec4<f32> {
    return vec4<f32>(%s, %s, %s, %s);
}
"""%tuple(f"{c:.8f}" for c in color)

def nq_source(color,name):
    return f'''module gpu.bench.{name}

export function shade(x: float, y: float): vec4 {{
    return vec4({color[0]}, {color[1]}, {color[2]}, {color[3]})
}}
'''

REFERENCES={
 "color_gradient": """
@fragment
fn main(@builtin(position) pos: vec4<f32>) -> @location(0) vec4<f32> {
  let x = u32(pos.x);
  let y = u32(pos.y);
  let p = ((x % 256u) * 65536u) + ((y % 256u) * 256u) + 64u;
  let red = f32((p >> 16u) & 255u) / 255.0;
  let green = f32((p >> 8u) & 255u) / 255.0;
  let blue = f32(p & 255u) / 255.0;
  return vec4<f32>(red,green,blue,1.0);
}
""",
 "posterize_fragment": """
@fragment
fn main(@builtin(position) pos: vec4<f32>) -> @location(0) vec4<f32> {
  let band=(u32(pos.x)+u32(pos.y)) % 256u;
  if band < 85u { return vec4<f32>(1.0,0.0,0.0,1.0); }
  if band < 170u { return vec4<f32>(0.0,1.0,0.0,1.0); }
  return vec4<f32>(0.0,0.0,1.0,1.0);
}
""",
 "vector_fragment": """
@fragment
fn main(@builtin(position) pos: vec4<f32>) -> @location(0) vec4<f32> {
  let nx=pos.x / 640.0;
  let ny=pos.y / 360.0;
  return vec4<f32>(nx,ny,0.25,1.0);
}
""",
 "vertex3d": """
@vertex
fn main(@location(0) position: vec4<f32>) -> @builtin(position) vec4<f32> {
  return vec4<f32>(position.x,position.y,position.z,position.w);
}
""",
 "fragment3d":wgsl_fragment((0.1,0.75,0.95,1.0)),
 "fragment_near":wgsl_fragment(NEAR),
}
for name,color in FACES:
    REFERENCES[name]=wgsl_fragment(color)

def main():
    OUTPUT.mkdir(exist_ok=True)
    for path in (NQ,REF,SOURCE):
        path.mkdir(exist_ok=True)
    compiler=ROOT/"build-gpu-compiler/noqeri"
    assert compiler.exists(),f"Noqeri compiler missing: {compiler}"
    cases=SHADES[:]
    for name,color in [("fragment_near",NEAR),*FACES]:
        path=SOURCE/f"{name}.noqeri"
        path.write_text(nq_source(color,name))
        cases.append((name,path,"fragment",[]))
    summary={"git_revision":call(["git","rev-parse","HEAD"]).strip(),
             "compiler_sha256":sha(compiler),
             "noqeri_sources":{},"shaders":[]}
    for name,src,stage,flags in cases:
        path=ROOT/src if isinstance(src,str) else src
        if not path.is_file():raise ValueError("missing shader source "+str(path))
        dst=NQ/f"{name}.wgsl"
        samples=[]
        expected_sha=None
        for i in range(-3,21):
            start=time.perf_counter_ns()
            call([compiler,"shader",path,dst,f"--stage={stage}",*flags])
            ms=(time.perf_counter_ns()-start)/1e6
            h=sha(dst)
            if expected_sha is not None and h!=expected_sha:
                raise RuntimeError("non-deterministic Noqeri shader emission "+name)
            expected_sha=h
            if i>=0:samples.append(ms)
        (REF/f"{name}.wgsl").write_text(REFERENCES[name])
        item={"name":name,"stage":stage,"source_sha256":sha(path),
              "noqeri_wgsl_sha256":expected_sha,
              "reference_wgsl_sha256":sha(REF/f"{name}.wgsl"),
              "wgsl_size":dst.stat().st_size,
              "median_compile_ms":statistics.median(samples),
              "p95_compile_ms":sorted(samples)[19],
              "raw_ms":samples}
        summary["shaders"].append(item)
        print("NQ_SHADER_COMPILE "+json.dumps(
            {k:item[k] for k in ("name","stage","median_compile_ms","wgsl_size")}),flush=True)
    (OUTPUT/"shader-compiler-results.json").write_text(json.dumps(summary,indent=2)+"\n")
    call(["cargo","run","--release","--quiet",
          "--manifest-path",ROOT/"tests/wgsl-validator/Cargo.toml",
          "--",*[NQ/f"{name}.wgsl" for name,*_ in cases]],timeout=600)
    print("NQ_SHADER_NAGA_VALIDATION_OK "+str(len(cases)),flush=True)

if __name__=="__main__":
    main()
