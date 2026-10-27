#!/usr/bin/env python3
"""Qualify browser-only async/input codegen from native C++ with Node absent."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
env=dict(os.environ,PATH="/intentionally-no-nodejs")
with tempfile.TemporaryDirectory(prefix="noqeri-native-load-") as temp:
    root=Path(temp)
    src=root/"source"
    src.mkdir()
    (src/"main.noqeri").write_text(
        'let name = "Initial"\n'
        'load greeting from "/api/greeting.json"\n'
        'view { main { h1 { "Async native view" } '
        'p { "Greeting: " + greeting.data.message } '
        'p { "Typed: " + name } '
        'input type: "text" on.input { name = form.value } {} '
        'form on.submit { name = form.username } { '
        'input type: "text" name: "username" {} '
        'button type: "submit" { "Save" } } } }\n')
    def run(*arguments):
        return subprocess.run([str(binary),*map(str,arguments)],env=env,
                              capture_output=True,text=True,timeout=30)
    build=run("build-client",src,root/"dist")
    assert build.returncode==0,(build.stdout,build.stderr)
    js=(root/"dist"/"main.mjs").read_text()
    html=(root/"dist"/"index.html").read_text()
    assert 'app.load("/api/greeting.json"' in js,js[:1300]
    assert "__nq_inputEvent" in js and "__nq_formFields" in js
    assert "__nq_member" in js
    assert '<main id="app"></main>' in html
    assert "Enable JavaScript" in html
    assert run("build",src,root/"static").returncode!=0,"async CSR incorrectly accepted as static SSR"
    assert run("serve-app",src,"18081",root/"state.nqkv").returncode!=0,"browser load accepted by SSR actions"

    cases=[
      ('load greeting from "https://evil.invalid/x"\nview { p { "No" } }','same-origin'),
      ('load greeting from "//evil.invalid/x"\nview { p { "No" } }','same-origin'),
      ('load greeting from "/../private"\nview { p { "No" } }','same-origin'),
      ('load greeting from "/api/%2fsecret"\nview { p { "No" } }','same-origin'),
      ('let name = "x"\nview { button on.input { name = form.value } { "Invalid" } }','matching'),
      ('let name = "x"\nview { input on.input { name = form.password } {} }','value/checked'),
      ('load data from "/api/greeting.json"\nlet data = "duplicate"\nview { p { "No" } }','duplicate'),
    ]
    for i,(source,error) in enumerate(cases):
        wrong=root/("invalid-"+str(i));wrong.mkdir()
        (wrong/"main.noqeri").write_text(source)
        output=root/("reject-"+str(i))
        result=run("build-client",wrong,output)
        assert result.returncode!=0 and error in result.stderr,(source,result.stdout,result.stderr)
        assert not (output/"index.html").exists()
print("PASS: native async JSON load and reactive controls build without Node, unsafe routes and unsupported semantics fail closed")
