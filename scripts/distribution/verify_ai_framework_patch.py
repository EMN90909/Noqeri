#!/usr/bin/env python3
"""Fail-closed integrity and provenance validation for v0.1.4 AI add-on SDKs."""
import hashlib
import json
import re
import struct
import sys
import tarfile
import zipfile
from pathlib import Path

TARGETS = (
    "linux-x86_64", "linux-aarch64",
    "windows-x86_64",
    "macos-x86_64", "macos-aarch64"
)
TAG = "50606b6c491dfde2c8a9f4db624a82f8839621b3"

def inspect(target, binary):
    if target.startswith("linux"):
        assert binary[:4] == b"\x7fELF" and binary[4] == 2 and binary[5] == 1, target
        machine=struct.unpack_from("<H",binary,18)[0]
        assert machine == (183 if target.endswith("aarch64") else 62), target
    elif target.startswith("windows"):
        assert binary[:2] == b"MZ", target
        offset=struct.unpack_from("<I",binary,0x3c)[0]
        assert binary[offset:offset+4] == b"PE\x00\x00"
        assert struct.unpack_from("<H",binary,offset+4)[0] == 0x8664,target
    else:
        assert binary[:4] in (b"\xcf\xfa\xed\xfe",b"\xfe\xed\xfa\xcf"),target
        endian="<" if binary[:4] == b"\xcf\xfa\xed\xfe" else ">"
        assert struct.unpack_from(endian+"I",binary,4)[0] == (
            0x0100000c if target.endswith("aarch64") else 0x01000007
        ),target

def verify(root, sha):
    assert re.fullmatch("[0-9a-f]{40}",sha),"invalid patch SHA"
    expect_names = {
        f"noqeri-v0.1.4-ai-framework-{target}."+
        ("zip" if target.startswith("windows") else "tar.gz")
        for target in TARGETS
    }
    assert {f.name for f in root.iterdir()} == expect_names,"unexpected/missing AI patch archive"
    sums={}
    for target in TARGETS:
        filename=f"noqeri-v0.1.4-ai-framework-{target}."+(
            "zip" if target.startswith("windows") else "tar.gz")
        archive=root/filename
        assert archive.is_file() and not archive.is_symlink()
        assert 1000<archive.stat().st_size<250_000_000,filename
        if target.startswith("windows"):
            with zipfile.ZipFile(archive) as z:
                assert z.testzip() is None,filename
                entries={info.filename:info for info in z.infolist()}
                assert all(((info.external_attr>>16)&0o170000)!=0o120000 for info in entries.values()), "ZIP contains symlink"
                def read(name):return z.read(name)
                checked_entries(archive,target,entries,read,sha)
        else:
            with tarfile.open(archive,"r:gz") as tar:
                entries={m.name.lstrip("./"):m for m in tar.getmembers()}
                assert all(m.isfile() or m.isdir() for m in entries.values()),"unsafe TAR entries"
                def read(name):
                    item=entries[name]
                    assert item.isfile() and item.size<4_000_000
                    return tar.extractfile(item).read()
                checked_entries(archive,target,entries,read,sha)
        sums[filename]=hashlib.sha256(archive.read_bytes()).hexdigest()
    (root/"SHA256SUMS-v014-ai-framework").write_text(
        "".join(f"{v}  {k}\n" for k,v in sorted(sums.items())))
    (root/"v0.1.4-ai-framework-manifest.json").write_text(json.dumps({
       "tag":"v0.1.4","tag_sha":TAG,"patch_source_sha":sha,
       "targets":list(TARGETS),"checksums":sums,
       "status":"experimental native CPU developer add-on",
       "note":"Additive v0.1.4 AI framework. Reverse-mode tensors, BPE and one-layer causal transformer on CPU. Not large-scale production LLM pretraining or GPU."
    },sort_keys=True,indent=2)+"\n")

def checked_entries(archive,target,entries,read,sha):
    ext=".exe" if target.startswith("windows") else ""
    required=[
        "patch/bin/noqeri"+ext,"patch/bin/noqeri-ai"+ext,
        "patch/include/noqeri/ai.hpp",
        "patch/include/noqeri/lm.hpp",
        "patch/include/noqeri/ai_framework.hpp",
        "patch/share/noqeri/Lib/ai/framework.noqeri",
        "patch/share/noqeri/Lib/std/numeric.noqeri",
        "patch/share/noqeri/Lib/ai/agent.noqeri",
        "patch/share/noqeri/Lib/ai/learn.noqeri",
        "patch/licenses/GPL-3.0-only.txt",
        "patch/licenses/Apache-2.0.txt",
        "patch/PATCH_SOURCE_SHA","patch/ORIGINAL_V014_TAG_SHA",
        "patch/README-AI-FRAMEWORK.md",
        "patch/lib/"+("noqeri_ai.lib" if ext else "libnoqeri_ai.a")
    ]
    assert all(r in entries for r in required),f"{archive} missing required patch files"
    assert read("patch/PATCH_SOURCE_SHA").decode().strip()==sha,archive
    assert read("patch/ORIGINAL_V014_TAG_SHA").decode().strip()==TAG,archive
    inspect(target,read("patch/bin/noqeri-ai"+ext))
    inspect(target,read("patch/bin/noqeri"+ext))

if __name__=="__main__":
    try:
        if len(sys.argv)!=3:
            raise ValueError("usage: verify_ai_framework_patch.py artifacts-dir patch-source-sha")
        verify(Path(sys.argv[1]),sys.argv[2])
    except (ValueError,AssertionError,OSError,tarfile.TarError,zipfile.BadZipFile,KeyError) as error:
        print("AI release verification rejected:",error,file=sys.stderr)
        sys.exit(2)
