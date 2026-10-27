#!/usr/bin/env python3
"""Check native Noqeri Motion compilation without Node and fail-closed errors."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

web=Path(sys.argv[1]).resolve()
env=dict(os.environ,PATH="/without-node-or-npm")
with tempfile.TemporaryDirectory(prefix="noqeri-motion-") as temp:
    root=Path(temp)
    def run(source,command,index):
        project=root/("src-"+str(index))
        project.mkdir()
        (project/"main.noqeri").write_text(source)
        dest=root/("dist-"+str(index))
        cmd=[str(web),command,str(project),str(dest)]
        if command=="serve-app":
            cmd=[str(web),command,str(project),"18889",str(dest)+".nqkv"]
        result=subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=30)
        return result,dest
    source=Path("examples/native_web_motion/main.noqeri").read_text()
    success,dest=run(source,"build-client","success")
    assert success.returncode==0,(success.stdout,success.stderr)
    js=(dest/"main.mjs").read_text()
    assert "__nq_createMotion(root" in js
    assert '"fromOpacity":0' in js
    assert '"trigger":"timeline"' in js
    assert 'steps:["hero","subtitle"]' in js
    assert "Noqeri Motion" in (dest/"index.html").read_text()
    assert "export function createMotion(" in (dest/"__noqeri/runtime.mjs").read_text()
    for mode in ("build","serve-app"):
        result,_=run(source,mode,mode)
        assert result.returncode!=0 and "build-client" in result.stderr,result.stderr
    invalid=[
        ('motion bad { target: "body" }\nview { p { "x" } }',"target"),
        ('motion bad { target: ".ok" duration: 30001 }\nview { p { "x" } }',"range"),
        ('motion bad { target: ".ok" ease: "eval" }\nview { p { "x" } }',"easing"),
        ('motion bad { target: ".ok" opacity: 0 }\nview { p { "x" } }',"unsupported"),
        ('motion bad { target: ".a" target: ".b" }\nview { p { "x" } }',"duplicate"),
        ('motion bad { target: ".a" trigger: "timeline" }\nview { p { "x" } }',"timeline"),
        ('motion bad { target: ".a" }\ntimeline intro { absent }\nview { p { "x" } }',"unknown"),
        ('motion bad { target: ".a" }\ntimeline intro { bad }\nview { p { "x" } }',"trigger timeline"),
        ('motion bad { target: ".a" trigger: "timeline" }\ntimeline a { bad }\ntimeline b { bad }\nview { p { "x" } }',"multiple"),
        ('motion bad { target: ".a" duration: 1.5 }\nview { p { "x" } }',"integer"),
    ]
    for i,(text,error) in enumerate(invalid):
        result,out=run(text,"build-client",i)
        assert result.returncode!=0 and error in result.stderr,(error,result.stderr)
        assert not (out/"index.html").exists()
print("PASS: motion native compiler, node-free browser artifacts, fail-closed parsing")
