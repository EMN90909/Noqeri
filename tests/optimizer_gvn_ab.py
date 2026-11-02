#!/usr/bin/env python3
"""Same-runner C++ Stage-0 optimizer A/B. Only compare identical NIR input."""
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: optimizer_gvn_ab.py baseline-binary candidate-binary")

executables = {"baseline": str(Path(sys.argv[1]).resolve()),
               "candidate": str(Path(sys.argv[2]).resolve())}
samples = {"baseline": [], "candidate": []}


def evaluate(which):
    result = subprocess.run([executables[which], "--bench"],
                            capture_output=True, text=True,
                            check=True, timeout=30)
    lines = result.stdout.splitlines()
    if "PASS: local GVN equivalence and cross-block safety" not in lines:
        raise AssertionError(f"{which}: correctness assertion missing")
    metrics = [row.split("=", 1)[1] for row in lines
               if row.startswith("GVN_BENCH_MS=")]
    if len(metrics) != 1:
        raise AssertionError(f"{which}: missing exclusive optimizer timing")
    value = float(metrics[0])
    if not 0 < value < 30000:
        raise AssertionError(f"{which}: invalid elapsed time")
    return value


for _ in range(3):
    for which in executables:
        evaluate(which)
for turn in range(15):
    for which in (("baseline", "candidate") if turn % 2 == 0
                  else ("candidate", "baseline")):
        samples[which].append(evaluate(which))

medians = {key: statistics.median(values) for key, values in samples.items()}
ratio = medians["baseline"] / medians["candidate"]
report = {
    "workload": "4000 duplicate integer NIR expressions with stores, O2",
    "scope": "Stage-0 optimizer wall-clock only; not compiled executable throughput",
    "host": platform.platform(),
    "cpu": platform.processor(),
    "source_base_sha": os.environ.get("BASE_SHA"),
    "source_head_sha": os.environ.get("HEAD_SHA"),
    "samples_ms": samples,
    "median_ms": medians,
    "baseline_over_candidate": ratio,
}
Path("optimizer-gvn-ab.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
if medians["candidate"] > medians["baseline"] * 1.5:
    raise SystemExit("Noqeri GVN optimizer median regression exceeds 50%.")
