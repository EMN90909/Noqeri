#!/usr/bin/env python3
"""Bounded, dependency-free development loop around the real Noqeri compiler.

No model or network access is required. An external coding agent edits source and
calls this tool to obtain machine-readable verification feedback. This tool never
applies generated changes and never deploys automatically.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time

def run_step(name, argv, root, timeout):
    started = time.monotonic()
    try:
        result = subprocess.run(argv, cwd=root, capture_output=True, text=True,
                                timeout=timeout, check=False)
        return {"step": name, "command": argv, "ok": result.returncode == 0,
                "exit_code": result.returncode, "stdout": result.stdout[-32000:],
                "stderr": result.stderr[-32000:],
                "duration_ms": round((time.monotonic() - started) * 1000)}
    except subprocess.TimeoutExpired:
        return {"step": name, "command": argv, "ok": False, "exit_code": None,
                "error": "time_limit_exceeded", "duration_ms": round(timeout * 1000)}
    except OSError as exc:
        return {"step": name, "command": argv, "ok": False, "exit_code": None,
                "error": str(exc)}

def main(argv=None):
    parser = argparse.ArgumentParser(description="Noqeri coding-agent validation loop")
    parser.add_argument("source", type=Path, help=".noqeri entry file")
    parser.add_argument("--compiler", default="noqeri", help="path to the compiled Noqeri binary")
    parser.add_argument("--tests", type=Path, help="optional test directory (only after check passes)")
    parser.add_argument("--build", action="store_true", help="also emit assembly after check passes")
    parser.add_argument("--output", type=Path, help="assembly output, defaults to build/agent-output.s")
    parser.add_argument("--timeout", type=int, default=30, help="timeout per step, in seconds")
    parser.add_argument("--max-attempts", type=int, default=1, help="number of validation attempts, maximum 10")
    args = parser.parse_args(argv)
    if args.timeout < 1 or not 1 <= args.max_attempts <= 10:
        parser.error("timeout must be positive and max-attempts must be between 1 and 10")
    root = Path.cwd().resolve()
    source = args.source.resolve()
    if source.suffix != ".noqeri" or not source.is_file():
        parser.error("source must be an existing .noqeri file")
    output = (args.output or Path("build/agent-output.s")).resolve()
    compiler = args.compiler
    if os.sep in compiler or (os.altsep and os.altsep in compiler):
        compiler = str(Path(compiler).resolve())
    attempts = []
    for number in range(1, args.max_attempts + 1):
        steps = [run_step("check", [compiler, "check", str(source), "--diagnostics=json"], root, args.timeout)]
        if steps[-1]["ok"] and args.tests:
            steps.append(run_step("test", [compiler, "test", str(args.tests.resolve())], root, args.timeout))
        if all(step["ok"] for step in steps) and args.build:
            output.parent.mkdir(parents=True, exist_ok=True)
            steps.append(run_step("build", [compiler, "build", str(source), str(output)], root, args.timeout))
        check = steps[0]
        diagnostics = []
        if check.get("stderr"):
            for line in check["stderr"].splitlines():
                try:
                    record = json.loads(line)
                    if isinstance(record, dict) and "code" in record:
                        diagnostics.append(record)
                except json.JSONDecodeError:
                    continue
        attempt = {"number": number, "ok": all(step["ok"] for step in steps),
                   "steps": steps, "diagnostics": diagnostics}
        attempts.append(attempt)
        # There is deliberately no automatic rewrite or busy retry: without a
        # new source edit the same failing compiler command cannot make progress.
        if attempt["ok"] or number == args.max_attempts:
            break
        break
    final = {"schema_version": 1, "language": "noqeri",
             "mode": "validate-only", "success": attempts[-1]["ok"],
             "attempts": attempts,
             "next_action": "ready_for_review" if attempts[-1]["ok"] else "edit_source_and_retry"}
    print(json.dumps(final, ensure_ascii=False, separators=(",", ":")))
    return 0 if final["success"] else 1

if __name__ == "__main__":
    sys.exit(main())
