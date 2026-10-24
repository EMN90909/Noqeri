#!/usr/bin/env python3
"""Annotate verified same-runner native A/B measurements with reproducible evidence.

Hardware perf counters and peak RSS may be unavailable in CI. Absence is
recorded explicitly, never silently represented as zero.
"""
import hashlib
import json
import math
import os
import platform
import shutil
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path


def percentiles(raw):
    samples = sorted(float(x) for x in raw)
    if len(samples) < 5 or any(not math.isfinite(x) or x < 0 for x in samples):
        raise ValueError("benchmark evidence must have at least 5 finite, nonnegative samples")
    p95_index = math.ceil(len(samples) * 0.95) - 1
    return {
        "count": len(samples),
        "p50_ms": statistics.median(samples),
        "p95_ms": samples[p95_index],
        "min_ms": samples[0],
        "max_ms": samples[-1],
        "mean_ms": statistics.mean(samples),
        "stddev_ms": statistics.pstdev(samples),
        "quantile_method": "nearest-rank",
    }


def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as inp:
        for part in iter(lambda: inp.read(65536), b""):
            h.update(part)
    return h.hexdigest()


def peak_rss(binary):
    tool = Path("/usr/bin/time")
    if not tool.exists():
        return {"status": "unavailable", "reason": "GNU time missing"}
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "rss.txt"
        result = subprocess.run(
            [str(tool), "-f", "%M", "-o", str(out), str(binary.resolve())],
            capture_output=True, timeout=20, check=False
        )
        if result.returncode != 0:
            return {"status": "unavailable", "reason": "native test failed"}
        try:
            return {"status": "measured-once", "peak_kib": int(out.read_text().strip())}
        except (OSError, ValueError):
            return {"status": "unavailable", "reason": "malformed time output"}


def perf_counters(binary):
    if not shutil.which("perf"):
        return {"status": "unavailable", "reason": "perf executable missing"}
    events = ["cycles", "instructions", "cache-misses", "branch-misses"]
    try:
        result = subprocess.run(
            ["perf", "stat", "-x,", "-e", ",".join(events), str(binary.resolve())],
            capture_output=True, text=True, check=False, timeout=20
        )
    except (OSError, subprocess.TimeoutExpired):
        return {"status": "unavailable", "reason": "perf invocation failed"}
    if result.returncode != 0:
        return {"status": "unavailable", "reason": "perf permissions or events unsupported"}
    found = {}
    for line in result.stderr.splitlines():
        cells = line.split(",")
        if len(cells) < 3:
            continue
        value, event = cells[0].strip(), cells[2].strip()
        if event in events:
            try:
                found[event] = int(value.replace(" ", ""))
            except ValueError:
                pass
    if len(found) != len(events):
        return {"status": "unavailable", "reason": "incomplete hardware counter data"}
    return {"status": "single-run-provisional", "counters": found}


def machine():
    model = None
    cpu = Path("/proc/cpuinfo")
    if cpu.exists():
        for line in cpu.read_text(errors="replace").splitlines():
            if line.lower().startswith("model name"):
                model = line.split(":", 1)[-1].strip()
                break
    return {
        "system": platform.system(),
        "kernel": platform.release(),
        "architecture": platform.machine(),
        "cpu_model": model or platform.processor() or "unknown",
        "logical_cpus": os.cpu_count(),
    }


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: native-perf-evidence.py <native-perf-ab.json>")
    report_path = Path(sys.argv[1])
    report = json.loads(report_path.read_text())
    cases = {"sorted_traversal": report.get("samples")}
    for key in ("dependent_modulo", "reshuffled_sorting"):
        if key not in report:
            continue
        item = report[key]
        cases[key] = item.get("samples_ms", item.get("samples"))
    summary = {}
    for name, case in cases.items():
        if not isinstance(case, dict):
            raise ValueError(f"{name}: missing sample evidence")
        summary[name] = {
            revision: percentiles(case[revision])
            for revision in ("baseline", "candidate")
        }
    binary_paths = {
        revision: Path(revision) / "build/aot-ab-sorting"
        for revision in ("baseline", "candidate")
    }
    artifacts = {}
    for revision, path in binary_paths.items():
        if not path.is_file():
            raise ValueError(f"missing standalone native artifact: {path}")
        artifacts[revision] = {
            "bytes": path.stat().st_size,
            "sha256": sha256(path),
            "peak_rss": peak_rss(path),
            "perf": perf_counters(path),
        }
    report["evidence_v2"] = {
        "machine": machine(),
        "quantiles": summary,
        "artifacts": artifacts,
        "basis": "same GitHub runner; native Stage-0, not native selfhost",
    }
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report["evidence_v2"], indent=2))


if __name__ == "__main__":
    main()
