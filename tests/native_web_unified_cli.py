#!/usr/bin/env python3
"""Qualified one-command Noqeri native framework entrypoint, no npm/Node."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

compiler=Path(sys.argv[1]).resolve()
web=Path(sys.argv[2]).resolve()
source=Path(sys.argv[3]).resolve()
env=os.environ.copy()
env["NOQERI_WEB_BIN"]=str(web)

def run(*args):
    return subprocess.run([str(compiler),"web",*args],env=env,
                          capture_output=True,text=True,timeout=110)

with tempfile.TemporaryDirectory() as td:
    root=Path(td)
    capabilities=run("drivers")
    assert capabilities.returncode==0,(capabilities.stdout,capabilities.stderr)
    for engine in ("noesql","sqlite","postgresql","duckdb","mysql"):
        assert engine+":" in capabilities.stdout
    assert "noqeri" not in capabilities.stderr.lower()
    output=root/"native-site"
    app=run("compile",str(source),str(output))
    assert app.returncode==0,(app.stdout,app.stderr)
    assert output.is_file() and os.access(output,os.X_OK)
    assert not (root/"native-site.cpp").exists()
    # Static rendering is served through the same entrypoint, not through a
    # second app registration, compiler CLI, npm, or JS bundler.
    static=root/"static"
    static_project=Path(sys.argv[4]).resolve()
    generated=run("build",str(static_project),str(static))
    assert generated.returncode==0,(generated.stdout,generated.stderr)
    assert "Noqeri UI v2" in (static/"index.html").read_text()
    assert (static/"theme.css").is_file()
    assert not list(static.rglob("*.js")) and not list(static.rglob("*.mjs"))
    # Backward-compatible checked web backend still emits an ES module when
    # given a source file, not a framework operation keyword.
    version=subprocess.run([str(compiler),"--version"],capture_output=True,text=True,timeout=3)
    assert version.returncode==0 and "noqeri 0.1.4" in version.stdout
print("PASS: unified Noqeri web CLI: native binary, static site, all database drivers, correct compiler version")
