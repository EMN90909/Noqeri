#!/usr/bin/env python3
"""Reproducible Noqeri compiler archives and a release manifest, using only Python stdlib.

Inputs are REAL compiled binaries. A cross-compiled binary is never marked tested
without an executed smoke-test evidence file. GPU vendor/ISA qualification is
distinct from the CPU binary's ability to emit WGSL.
"""
from __future__ import annotations
import argparse
import gzip
import hashlib
import io
import json
import os
import re
import stat
import sys
import tarfile
import zipfile
from datetime import datetime, timezone
from pathlib import Path

VERSION = re.compile(r"^(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)(?:-[0-9A-Za-z.-]+)?$")
SHA = re.compile(r"^[0-9a-f]{40}$")
SAFE_NAME = re.compile(r"^[a-zA-Z0-9][a-zA-Z0-9._-]{0,127}$")
OS = {"linux", "windows", "macos", "android", "freebsd", "openbsd"}
CPU = {"x86_64", "arm64", "armv7", "x86", "riscv64", "ppc64le", "s390x"}
FORMATS = ("binary", "gz", "tar", "tar.gz", "zip")
INSTALLERS = ("deb", "rpm", "msi", "setup.exe", "pkg", "pkg.tgz", "termux.deb", "apk")
MAX_BYTES = 1024 * 1024 * 1024

def fail(message):
    raise ValueError(message)

def file_hash(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for part in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(part)
    return h.hexdigest()

def canonical_json(value):
    return json.dumps(value, sort_keys=True, indent=2, ensure_ascii=False) + "\n"

def valid_target(os_name, cpu):
    return (os_name in OS or bool(re.fullmatch(r"custom-[a-z0-9][a-z0-9-]{0,31}", os_name))) and cpu in CPU

def epoch_value():
    value = int(os.environ.get("SOURCE_DATE_EPOCH", "315532800"))
    if value < 0:
        fail("SOURCE_DATE_EPOCH must be nonnegative")
    return value

def bundle(args):
    if not VERSION.fullmatch(args.version) or not valid_target(args.os, args.cpu):
        fail("invalid version, OS or CPU")
    if not SHA.fullmatch(args.source_commit):
        fail("source commit must be a full 40-character Git SHA")
    binary = Path(args.binary).resolve()
    if not binary.is_file() or binary.is_symlink() or not 0 < binary.stat().st_size <= MAX_BYTES:
        fail("compiler binary must be an existing nonempty regular file <=1GiB")
    if args.os == "windows" and binary.suffix.lower() != ".exe":
        fail("Windows compiler must be a PE .exe file")
    if args.qualification == "executed" and not args.smoke_evidence:
        fail("executed binaries require a smoke-test evidence file")
    if not re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,31}", args.gpu_arch):
        fail("invalid GPU architecture identifier")
    if (args.gpu_arch != "generic" or args.gpu_backend not in ("cpu-fallback", "wgsl-emitter")) and not args.gpu_evidence:
        fail("GPU-specific builds require hardware-driver evidence")
    for evidence in (args.smoke_evidence, args.gpu_evidence):
        if evidence and (not Path(evidence).is_file() or Path(evidence).stat().st_size == 0):
            fail("nonempty qualification evidence file missing")
    target = f"{args.os}-{args.cpu}" + (f"-{args.gpu_arch}" if args.gpu_arch != "generic" else "")
    output = Path(args.output).resolve() / target
    if output.exists() and any(output.iterdir()):
        fail("refusing to overwrite an existing target bundle")
    output.mkdir(parents=True, exist_ok=True)
    base = f"noqeri-v{args.version}-{target}"
    exe = "noqeri.exe" if args.os == "windows" else "noqeri"
    payload = binary.read_bytes()
    note = (f"Noqeri v{args.version}\nTarget: {target}\nQualification: {args.qualification}\n"
            "Stage-0 bootstrap compiler. Not native Stage-2 self-hosted.\n"
            "GPU driver qualification is separate from WGSL shader emission.\n"
            "Extract and run bin/noqeri (Windows: bin/noqeri.exe).\n").encode()
    members = [(f"{base}/bin/{exe}", payload, True), (f"{base}/README.txt", note, False)]
    if args.license:
        license_path = Path(args.license)
        if not license_path.is_file():
            fail("license file missing")
        members.append((f"{base}/LICENSE", license_path.read_bytes(), False))
    if args.sdk_root:
        sdk=Path(args.sdk_root).resolve()
        if not sdk.is_dir(): fail("installed SDK root does not exist")
        copied=sum(len(data) for _,data,_ in members)
        for item in sorted(sdk.rglob("*")):
            if item.is_symlink(): fail("installed SDK symlinks are not supported in portable archives")
            if item.is_dir(): continue
            if not item.is_file(): fail("installed SDK contains a special file")
            relative=item.relative_to(sdk).as_posix()
            if ".." in item.relative_to(sdk).parts or relative.startswith("/") or "\\u0000" in relative:
                fail("invalid installed SDK path")
            if relative=="bin/"+exe: continue
            data=item.read_bytes()
            copied+=len(data)
            if copied>MAX_BYTES: fail("SDK archive uncompressed limit exceeded")
            members.append((f"{base}/{relative}",data,relative.startswith("bin/")))
    members.sort(key=lambda item: item[0])
    timestamp = epoch_value()
    entries = []
    for fmt in args.formats.split(","):
        if fmt not in FORMATS or any(item["format"] == fmt for item in entries):
            fail("invalid/duplicate format; choose binary,gz,tar,tar.gz,zip")
        if fmt == "binary":
            name, content = base + (".exe" if args.os == "windows" else "-bin"), payload
        elif fmt == "gz":
            name = base + (".exe.gz" if args.os == "windows" else "-bin.gz")
            sink = io.BytesIO()
            with gzip.GzipFile(filename="", fileobj=sink, mode="wb", compresslevel=9, mtime=0) as stream:
                stream.write(payload)
            content = sink.getvalue()
        elif fmt == "zip":
            name = base + ".zip"
            sink = io.BytesIO()
            with zipfile.ZipFile(sink, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as stream:
                dt = datetime.fromtimestamp(max(timestamp, 315532800), timezone.utc)
                stamp = (dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second)
                for path, data, executable in members:
                    info = zipfile.ZipInfo(path, date_time=stamp)
                    info.create_system = 3
                    info.external_attr = ((0o100755 if executable else 0o100644) << 16)
                    info.compress_type = zipfile.ZIP_DEFLATED
                    stream.writestr(info, data)
            content = sink.getvalue()
        else:
            name = base + (".tar.gz" if fmt == "tar.gz" else ".tar")
            sink = io.BytesIO()
            with tarfile.open(fileobj=sink, mode="w", format=tarfile.PAX_FORMAT) as stream:
                for path, data, executable in members:
                    info = tarfile.TarInfo(path)
                    info.size = len(data)
                    info.mode = 0o755 if executable else 0o644
                    info.mtime = timestamp
                    stream.addfile(info, io.BytesIO(data))
            if fmt == "tar.gz":
                zipped = io.BytesIO()
                with gzip.GzipFile(filename="", fileobj=zipped, mode="wb", compresslevel=9, mtime=0) as stream:
                    stream.write(sink.getvalue())
                content = zipped.getvalue()
            else:
                content = sink.getvalue()
        path = output / name
        path.write_bytes(content)
        if fmt == "binary" and args.os != "windows":
            path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        entries.append({"filename": name, "format": fmt, "bytes": path.stat().st_size, "sha256": file_hash(path)})
    result = {
        "schema": 1, "version": args.version, "source_commit": args.source_commit,
        "os": args.os, "cpu": args.cpu, "qualification": args.qualification,
        "gpu": {"backend": args.gpu_backend, "architecture": args.gpu_arch,
                "hardware_qualified": bool(args.gpu_evidence)},
        "smoke_evidence": Path(args.smoke_evidence).name if args.smoke_evidence else None,
        "assets": entries,
    }
    (output / "fragment.json").write_text(canonical_json(result), encoding="utf-8")
    print(f"Bundled {target}: {len(entries)} real files, {args.qualification}")

def validate_asset(root, target, item):
    if not isinstance(item, dict) or not isinstance(item.get("filename"), str) or not SAFE_NAME.fullmatch(item["filename"]):
        fail("unsafe asset filename")
    if item.get("format") not in FORMATS + INSTALLERS:
        fail("invalid asset format")
    path = root / target / item["filename"]
    if path.is_symlink() or not path.is_file() or path.stat().st_size != item.get("bytes"):
        fail("missing or size-mismatched asset: " + str(path))
    if not isinstance(item.get("sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", item["sha256"]):
        fail("invalid SHA-256")
    if file_hash(path) != item["sha256"]:
        fail("asset SHA-256 mismatch: " + str(path))
    if item["format"] == "zip":
        with zipfile.ZipFile(path) as archive:
            if not archive.namelist() or any(p.startswith("/") or ".." in Path(p).parts for p in archive.namelist()):
                fail("unsafe ZIP members")
    elif item["format"] in ("tar", "tar.gz"):
        with tarfile.open(path) as archive:
            if not archive.getmembers() or any(not m.isfile() or m.name.startswith("/") or ".." in Path(m.name).parts for m in archive.getmembers()):
                fail("unsafe tar members")

def combine(args):
    root = Path(args.input).resolve()
    fragments = sorted(root.glob("*/fragment.json"))
    if not fragments:
        fail("no real compiler artifacts were built")
    targets, seen, checksums = [], set(), []
    for path in fragments:
        data = json.loads(path.read_text(encoding="utf-8"))
        if (data.get("schema") != 1 or data.get("version") != args.version or
                not valid_target(data.get("os", ""), data.get("cpu", "")) or
                not isinstance(data.get("source_commit"), str) or not SHA.fullmatch(data["source_commit"])):
            fail("invalid target fragment, version or source revision")
        if args.source_commit and data["source_commit"] != args.source_commit:
            fail("source revision does not match")
        target = data["os"] + "-" + data["cpu"] + (("-" + data.get("gpu", {}).get("architecture", "generic")) if data.get("gpu", {}).get("architecture", "generic") != "generic" else "")
        if target in seen or path.parent.name != target:
            fail("duplicate/misnamed target")
        seen.add(target)
        if data.get("qualification") not in ("executed", "build-only"):
            fail("invalid qualification")
        if data["qualification"] == "executed" and not data.get("smoke_evidence"):
            fail("target execution evidence required")
        gpu = data.get("gpu")
        if not isinstance(gpu, dict) or not isinstance(gpu.get("architecture"), str):
            fail("GPU metadata missing")
        assets = data.get("assets")
        if not isinstance(assets, list) or not assets:
            fail("target has no assets")
        filenames = set()
        for asset in assets:
            validate_asset(root, target, asset)
            if asset["filename"] in filenames:
                fail("duplicate artifact")
            filenames.add(asset["filename"])
            asset["path"] = target + "/" + asset["filename"]
            checksums.append((asset["sha256"], asset["path"]))
        targets.append(data)
    for needed in args.require.split(",") if args.require else ():
        if needed not in seen:
            fail("required target absent: " + needed)
    if len({t["source_commit"] for t in targets}) != 1:
        fail("release assembled from inconsistent source commits")
    if args.stable and any(t["qualification"] != "executed" for t in targets):
        fail("stable release cannot include cross-build-only targets")
    output = {
        "schema": 1, "product": "noqeri-compiler", "version": args.version,
        "source_commit": targets[0]["source_commit"],
        "channel": "stable" if args.stable else "preview",
        "targets": sorted(targets, key=lambda t: (t["os"], t["cpu"])),
    }
    (root / "release-manifest.json").write_text(canonical_json(output), encoding="utf-8")
    (root / "checksums.txt").write_text("".join(f"{sha}  {path}\n" for sha,path in sorted(checksums)), encoding="utf-8")
    print(f"Verified {len(targets)} target bundles and {len(checksums)} artifacts")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="command", required=True)
    pack = actions.add_parser("bundle")
    for name in ("binary", "version", "os", "cpu", "source-commit", "output"):
        pack.add_argument("--" + name, required=True)
    pack.add_argument("--license")
    pack.add_argument("--sdk-root", help="CMake-installed SDK directory included in tar/zip bundles")
    pack.add_argument("--formats", default="binary,gz,tar,tar.gz,zip")
    pack.add_argument("--qualification", choices=("executed", "build-only"), default="build-only")
    pack.add_argument("--smoke-evidence")
    pack.add_argument("--gpu-backend", choices=("cpu-fallback", "wgsl-emitter", "vulkan", "metal", "cuda", "hip", "direct3d12", "webgpu", "opencl"), default="cpu-fallback")
    pack.add_argument("--gpu-arch", default="generic")
    pack.add_argument("--gpu-evidence")
    pack.set_defaults(run=bundle)
    merger = actions.add_parser("combine")
    for name in ("input", "version"):
        merger.add_argument("--" + name, required=True)
    merger.add_argument("--source-commit")
    merger.add_argument("--require", default="")
    merger.add_argument("--stable", action="store_true")
    merger.set_defaults(run=combine)
    args = parser.parse_args()
    try:
        args.run(args)
    except (OSError, ValueError, json.JSONDecodeError, tarfile.TarError, zipfile.BadZipFile) as error:
        print(f"distribution: {error}", file=sys.stderr)
        sys.exit(2)

if __name__ == "__main__":
    main()
