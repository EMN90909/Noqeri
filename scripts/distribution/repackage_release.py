#!/usr/bin/env python3
"""Turn real, built Noqeri SDK archives into manifest-qualified download formats.

Runs only AFTER the existing multi-platform build/test matrix succeeds.
Source archives must contain one stage/ tree with the actual compiler binary.
Never trusts archive paths, links or claimed CPU architecture.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path

TARGETS = (
    "linux-x86", "linux-x86_64", "linux-aarch64",
    "windows-x86", "windows-x86_64", "windows-aarch64",
    "macos-x86_64", "macos-aarch64",
    "android-armv7", "android-aarch64", "android-x86", "android-x86_64",
    "freebsd-x86_64", "freebsd-aarch64",
    "openbsd-x86_64", "openbsd-aarch64",
)
ARCH = {"aarch64": "arm64"}
MAX_FILE = 1024 * 1024 * 1024
MAX_TOTAL = 1536 * 1024 * 1024
MAX_FILES = 8192
TOOLS = Path(__file__).resolve().parents[1] / "distribution.py"

def fail(msg):
    raise ValueError(msg)

def checked_path(name):
    if not name.startswith("stage/") or name.startswith("/") or "\\" in name or "\0" in name:
        fail("archive path is not rooted in stage/")
    parts=name.rstrip("/").split("/")
    if len(parts)<2 or any(part in ("", ".", "..") for part in parts):
        fail("unsafe path in SDK archive")
    return parts

def extract_sdk(archive, target, dest):
    expected=".zip" if target.startswith("windows-") else ".tar.gz"
    if not archive.name.endswith(expected): fail("wrong archive format for OS")
    entries, consumed = 0, 0

    def extract_one(name, size, open_file, directory=False):
        nonlocal entries, consumed
        # cmake -E tar includes the stage/ root directory itself. Accept it
        # only as an actual directory; reject a stage file or out-of-root path.
        if directory and name in ("stage", "stage/"):
            return
        parts=checked_path(name)
        if directory: return
        entries+=1
        consumed+=size
        if entries>MAX_FILES or size>MAX_FILE or consumed>MAX_TOTAL:
            fail("SDK extraction resource budget exceeded")
        output=dest.joinpath(*parts)
        output.parent.mkdir(parents=True,exist_ok=True)
        with open_file() as src,output.open("wb") as out:
            transferred=0
            while True:
                data=src.read(1024 * 1024)
                if not data: break
                transferred+=len(data)
                if transferred>size: fail("SDK file exceeds advertised size")
                out.write(data)
            if transferred!=size: fail("SDK member truncated")

    if target.startswith("windows-"):
        with zipfile.ZipFile(archive) as z:
            for member in z.infolist():
                kind=(member.external_attr>>16)&0o170000
                if kind not in (0,0o040000,0o100000): fail("SDK ZIP contains symlink/special file")
                extract_one(member.filename,member.file_size,lambda m=member:z.open(m),member.is_dir())
    else:
        with tarfile.open(archive,mode="r:gz") as tar:
            for member in tar:
                if not member.isdir() and not member.isfile():
                    fail("SDK TAR contains links or special files")
                extract_one(member.name,member.size,lambda m=member:tar.extractfile(m),member.isdir())
    stage=dest/"stage"
    exe="noqeri.exe" if target.startswith("windows-") else "noqeri"
    compiler=stage/"bin"/exe
    vm=stage/"bin"/("noqeri-nqri.exe" if target.startswith("windows-") else "noqeri-nqri")
    if not compiler.is_file() or not vm.is_file():
        fail("SDK archive missing real compiler or NQRI virtual machine")
    if not (stage/"share/noqeri/Lib").is_dir():
        fail("SDK archive missing Noqeri standard library")
    if not (stage/"include/noqeri").is_dir():
        fail("SDK archive missing public C/C++ ABI headers")
    return stage,compiler

def include_native_installers(source_dir, release_dir, version, target):
    """Only accept the exact installer names qualified by the native matrix."""
    os_name,_=target.split("-",1)
    assets=[]
    base=f"noqeri-{version}-{target}"
    if os_name=="linux":
        assets=[(base+".deb","deb"),(base+".rpm","rpm")]
    elif os_name=="windows":
        assets=[(base+"-setup.exe","setup.exe")]
        if target in ("windows-x86","windows-x86_64"):
            assets.append((base+".msi","msi"))
    elif os_name in ("macos","freebsd"):
        assets=[(base+".pkg","pkg")]
    elif os_name=="openbsd":
        assets=[(base+".pkg.tgz","pkg.tgz")]
    elif os_name=="android":
        assets=[(base+"-termux.deb","termux.deb"),
                (f"noqeri-{version}-android-universal-debug.apk","apk")]
    if not assets:
        fail("no native installers expected for "+target)
    manifest=release_dir/"fragment.json"
    fragment=json.loads(manifest.read_text(encoding="utf-8"))
    for name,fmt in assets:
        file=source_dir/name
        if not file.is_file() or file.is_symlink() or not 0<file.stat().st_size<=MAX_FILE:
            fail("missing, empty or unsafe native installer: "+name)
        copied=release_dir/name
        if copied.exists(): fail("installer file would overwrite a compiler artifact")
        shutil.copyfile(file,copied)
        digest=hashlib.sha256()
        with copied.open("rb") as stream:
            for chunk in iter(lambda:stream.read(1024*1024),b""):
                digest.update(chunk)
        fragment["assets"].append({
            "filename":name,"format":fmt,
            "bytes":copied.stat().st_size,"sha256":digest.hexdigest()
        })
    manifest.write_text(json.dumps(fragment,indent=2,sort_keys=True)+"\n",encoding="utf-8")

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--license", default="LICENSE")
    parser.add_argument("--targets", default=",".join(TARGETS))
    parser.add_argument("--include-installers", action="store_true",
                        help="Require and hash actual OS-native package artifacts too")
    args=parser.parse_args()
    root=Path(args.input).resolve()
    output=Path(args.output).resolve()
    expected=[x.strip() for x in args.targets.split(",") if x.strip()]
    if not expected or len(set(expected))!=len(expected) or any(x not in TARGETS for x in expected):
        fail("invalid target matrix")
    with tempfile.TemporaryDirectory(prefix="noqeri-release-") as scratch:
        work=Path(scratch)
        for target in expected:
            os_name,cpu=target.split("-",1)
            source=root/("noqeri-v"+args.version+"-"+target+(".zip" if os_name=="windows" else ".tar.gz"))
            if not source.is_file() or source.is_symlink() or not 0<source.stat().st_size<=MAX_FILE:
                fail("missing or invalid native SDK archive: "+str(source))
            stage,compiler=extract_sdk(source,target,work/target)
            cmd=[
                sys.executable,str(TOOLS),"bundle", "--binary",str(compiler),
                "--version",args.version,"--os",os_name,"--cpu",ARCH.get(cpu,cpu),
                "--source-commit",args.source_commit,"--output",str(output),
                "--sdk-root",str(stage),"--license",args.license,
                "--qualification","build-only","--gpu-backend","cpu-fallback",
            ]
            subprocess.run(cmd,check=True)
            if args.include_installers:
                include_native_installers(root,output/(os_name+"-"+ARCH.get(cpu,cpu)),args.version,target)
    print("Repackaged real native SDKs, in 5 formats each: "+", ".join(expected))

if __name__=="__main__":
    try: main()
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError, zipfile.BadZipFile, tarfile.TarError) as exc:
        print("SDK release conversion refused: "+str(exc),file=sys.stderr)
        sys.exit(2)
