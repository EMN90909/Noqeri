#!/usr/bin/env python3
"""Prove C++ Noqeri client-ESM builds with Node absent, and unsupported effects fail closed."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

web=Path(sys.argv[1]).resolve()
source=Path(sys.argv[2]).resolve()
env=dict(os.environ,PATH="/no-node-or-other-tools")
with tempfile.TemporaryDirectory(prefix="noqeri-native-csr-") as tmp:
    directory=Path(tmp)
    output=directory/"dist"
    run=subprocess.run([str(web),"build-client",str(source),str(output)],
                       env=env,text=True,capture_output=True,timeout=30)
    assert run.returncode==0,(run.stdout,run.stderr)
    html=(output/"index.html").read_text()
    about=(output/"about/index.html").read_text()
    script=(output/"main.mjs").read_text()
    runtime=(output/"__noqeri/runtime.mjs").read_text()
    assert "<main id=\"app\"><div>" in html and "Count: 0" in html
    assert "Noqeri native client views" in html
    assert "Client-side routing without Node.js" in about
    assert 'src="main.mjs"' in html and 'src="../main.mjs"' in about
    assert 'import {element as __nq_h' in script
    assert "window.eval(" not in script and "eval(" not in script
    assert "Number(__nq_state" in script
    assert "export function mount(root)" in script
    assert "export function createApp" in runtime
    # Browser local-state form submissions are now supported, but privileged
    # server-side validators are still rejected by the native CSR compiler.
    bad=directory/"bad";bad.mkdir()
    (bad/"main.noqeri").write_text(
        'let title = "Hello"\n'
        'view { form on.submit { validateEmail(displayName) } '
        '{ input name: "displayName" {} button type: "submit" { "Save" } } }\n')
    rejected=subprocess.run([str(web),"build-client",str(bad),str(directory/"bad-dist")],
                            env=env,text=True,capture_output=True,timeout=30)
    assert rejected.returncode!=0 and "validation/database actions require serve-app" in rejected.stderr,rejected.stderr
    assert not (directory/"bad-dist"/"index.html").exists()
    # Deployment path should not be writable inside the source tree.
    rejected=subprocess.run([str(web),"build-client",str(source),str(source/"dist")],
                            env=env,text=True,capture_output=True,timeout=30)
    assert rejected.returncode!=0 and "outside source" in rejected.stderr,rejected.stderr
print("PASS: native C++ browser ESM compile, static route prerender, no Node, unsafe client effects rejected")
