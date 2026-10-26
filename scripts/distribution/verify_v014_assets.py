#!/usr/bin/env python3
"""Verify CI-built v0.1.4 OS/CPU downloads; never relabel a different CPU.

Run only on artifacts from a trusted main-branch distribution workflow.
The release tag is immutable; the manifest exposes the exact later build SHA.
"""
import hashlib
import json
import re
import struct
import sys
import tarfile
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
MACHINES = {"x86": 3, "x86_64": 62, "aarch64": 183, "armv7": 40}
PE_MACHINES = {"x86": 0x14c, "x86_64": 0x8664, "aarch64": 0xaa64}
MACHO_MACHINES = {"x86_64": 0x01000007, "aarch64": 0x0100000c}
MAX_FILE_BYTES = 700 * 1024 * 1024

def fail(message):
    raise ValueError(message)

def inspect_binary(path, target):
    _, arch = target.split("-", 1)
    raw = path.read_bytes()
    if len(raw) < 128 or len(raw) > MAX_FILE_BYTES:
        fail(f"invalid native executable size {path.name}")
    if target.startswith("windows-"):
        if raw[:2] != b"MZ":
            fail("invalid PE header " + path.name)
        offset = struct.unpack_from("<I", raw, 0x3c)[0]
        if offset + 6 > len(raw) or raw[offset:offset+4] != b"PE\0\0":
            fail("missing PE machine header")
        machine = struct.unpack_from("<H", raw, offset+4)[0]
        if machine != PE_MACHINES[arch]:
            fail(f"PE machine {machine} does not match {target}")
    elif target.startswith("macos-"):
        if raw[:4] not in (b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf"):
            fail("invalid Mach-O 64 header")
        endian = "<" if raw[:4] == b"\xcf\xfa\xed\xfe" else ">"
        cpu = struct.unpack_from(endian+"I",raw,4)[0]
        if cpu != MACHO_MACHINES[arch]:
            fail(f"Mach-O CPU {cpu} does not match {target}")
    else:
        if raw[:4] != b"\x7fELF":
            fail("invalid ELF header " + path.name)
        bits = 1 if arch in ("x86", "armv7") else 2
        if raw[4] != bits or raw[5] != 1:
            fail(f"incorrect ELF bitness/endianness for {target}")
        machine = struct.unpack_from("<H",raw,18)[0]
        if machine != MACHINES[arch]:
            fail(f"ELF machine {machine} does not match {target}")

def validate_sdk(archive, target):
    if target.startswith("windows-"):
        with zipfile.ZipFile(archive) as data:
            names = {i.filename.lstrip("./") for i in data.infolist()}
            if data.testzip() is not None:
                fail("broken ZIP for "+target)
    else:
        with tarfile.open(archive,"r:gz") as data:
            if any(i.issym() or i.islnk() or i.isdev() for i in data.getmembers()):
                fail("SDK archive includes links/device nodes: "+target)
            names = {i.name.lstrip("./") for i in data.getmembers()}
    ext=".exe" if target.startswith("windows-") else ""
    for item in ("stage/bin/noqeri"+ext,
                 "stage/bin/noqeri-nqri"+ext,
                 "stage/bin/noqeri-web"+ext,
                 "stage/share/noqeri/Lib/web/ui.noqeri",
                 "stage/share/noqeri/LICENSE",
                 "stage/share/noqeri/licenses/Apache-2.0.txt"):
        if item not in names:
            fail(f"SDK {target} missing {item}")

def main():
    if len(sys.argv) != 5:
        fail("usage: verify_v014_assets.py ARTIFACTS SOURCE_SHA TAG_SHA RUN_ID")
    directory = Path(sys.argv[1]).resolve()
    source_sha,tag_sha,run_id = sys.argv[2:]
    if not re.fullmatch(r"[0-9a-f]{40}",source_sha) or not re.fullmatch(r"[0-9a-f]{40}",tag_sha):
        fail("invalid build or immutable tag SHA")
    if not run_id.isdecimal():
        fail("invalid workflow run identifier")
    files = [x for x in directory.iterdir() if x.is_file() and not x.is_symlink()]
    for file in files:
        if not file.name.startswith("noqeri-") or not (file.name.startswith("noqeri-v0.1.4-")
                          or file.name.startswith("noqeri-nqri-v0.1.4-")
                          or file.name.startswith("noqeri-0.1.4-")):
            fail("unexpected file in CI output: "+file.name)
        if file.stat().st_size < 100 or file.stat().st_size > MAX_FILE_BYTES:
            fail("empty or oversized release file: "+file.name)
    known = {f.name for f in files}
    qualified = []
    for target in TARGETS:
        prefix=f"noqeri-v0.1.4-{target}"
        archive=directory/(prefix+(".zip" if target.startswith("windows-") else ".tar.gz"))
        compiler=directory/(prefix+(".exe" if target.startswith("windows-") else ".bin"))
        vm=directory/(f"noqeri-nqri-v0.1.4-{target}"+(".exe" if target.startswith("windows-") else ".bin"))
        group=(archive,compiler,vm)
        present=[x.exists() for x in group]
        if any(present) and not all(present):
            fail("incomplete OS/CPU SDK group: "+target)
        if all(present):
            inspect_binary(compiler,target)
            inspect_binary(vm,target)
            validate_sdk(archive,target)
            qualified.append(target)
    if not qualified:
        fail("no qualified target SDKs to publish")
    apk=directory/"noqeri-0.1.4-android-universal-debug.apk"
    if apk.exists():
        with zipfile.ZipFile(apk) as z:
            if z.testzip() is not None:
                fail("corrupt Android APK")
            names=set(z.namelist())
            for abi in ("armeabi-v7a","arm64-v8a","x86","x86_64"):
                if f"lib/{abi}/libnoqeri_android.so" not in names:
                    fail("APK missing native ABI "+abi)
    hashes={x.name:hashlib.sha256(x.read_bytes()).hexdigest() for x in files}
    manifest={
        "release":"v0.1.4",
        "release_tag_commit":tag_sha,
        "package_source_commit":source_sha,
        "distribution_workflow_run":run_id,
        "qualification":"CI-built and architecture-inspected; Android CLI cross-build only",
        "available":sorted(qualified),
        "unavailable":sorted(set(TARGETS)-set(qualified)),
        "android_debug_apk":apk.exists(),
        "files":hashes,
        "note":"The release tag is unchanged. Additional platform builds may come from a later fixing commit; use package_source_commit for exact source provenance."
    }
    (directory/"v0.1.4-platform-manifest.json").write_text(json.dumps(manifest,indent=2,sort_keys=True)+"\n")
    checksum="\n".join(f"{v}  {k}" for k,v in sorted(hashes.items()))+"\n"
    (directory/"SHA256SUMS-platform-v014").write_text(checksum)
    print("Qualified:",", ".join(qualified))
    print("Unavailable:",", ".join(manifest["unavailable"]))
    print("APK:",apk.exists())
if __name__=="__main__":
    try:
        main()
    except (ValueError,OSError,zipfile.BadZipFile,tarfile.TarError) as error:
        print("v0.1.4 platform release verification FAILED:",error,file=sys.stderr)
        sys.exit(2)
