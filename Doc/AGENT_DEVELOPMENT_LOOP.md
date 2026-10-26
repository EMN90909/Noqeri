# Noqeri agent development loop (developer preview)

Noqeri remains a **general-purpose language**. This optional agent interface wraps the existing Stage-0 compiler. It is not new language syntax, not an LLM provider, and not a replacement compiler. It runs offline with Node.js 20+ and a locally built C++17 Stage-0 compiler.

## Beginner-friendly compiler errors

Run:

    noqeri check src/main.noqeri

Errors now show their code, original file, one-based line and column, relevant source line with caret underline, and an actionable help message. Imported modules retain their original file locations.

For machine-readable output:

    noqeri check src/main.noqeri --json

Failure exits nonzero and writes one JSON object to stdout. Example schema (illustrative, not a live compiler run):

    {
      "schema": 1,
      "ok": false,
      "diagnostics": [{
        "severity": "error",
        "code": "NQR-T3002",
        "message": "unknown symbol 'missing'",
        "file": "/workspace/src/main.noqeri",
        "span": { "start": 13, "end": 20, "line": 1, "column": 14 },
        "help": "Declare this name with let or const before using it, or check the spelling."
      }]
    }

Spans use zero-based **byte** start/end offsets and one-based line/column. Unicode graphemes and tabs may not align exactly in terminal highlights. Success returns a schema-1 object with ok=true, an empty diagnostics array, and a modules count. Non-check CLI commands retain their existing output. This is a **Stage-0** capability; Stage-1/2 parity has not been established.

## Bounded agent development loop

Build the normal compiler, then start the agent host:

    cmake -S . -B build -DNOQERI_GRAPHICS_PORTABLE_BUILD=ON -DBUILD_TESTING=ON
    cmake --build build --target noqeri
    node Tools/agent/loop.mjs --compiler ./build/noqeri --workspace ./demo --max-steps 12

The workspace directory must exist first. The host accepts one JSON request per line on **stdin** and replies with one JSON response per line on **stdout**, echoing an optional request id. A coding agent decides how to fix errors, then submits another source edit. Example request lines:

    {"id":1,"action":"write_check","path":"src/main.noqeri","content":"let answer = missing\n"}
    {"id":2,"action":"write_check","path":"src/main.noqeri","content":"let answer = 42\nprint(answer)\n"}
    {"id":3,"action":"build","path":"src/main.noqeri","format":"nqri","output":"build/main.nqri"}
    {"id":4,"action":"stage","artifact":"build/main.nqri","output":"dist/main.nqri"}

The first response contains diagnostics; the second compiles the repair; the third emits portable NQRI; and the fourth stages a SHA-256-hashed local artifact. Staging is **not remote deployment**.

### Available actions

| Action | Input | Purpose |
| --- | --- | --- |
| help | none | Report protocol and permissions |
| read | path | Read a bounded .noqeri file |
| write_check | path, content | Atomically write source, then compile |
| check | path | Compile and report structured diagnostics |
| build | path, format, output | Emit .nqri (nqri) or .s (assembly) |
| stage | artifact, optional output | Copy artifact into dist/ and report hash |
| run | path | Execute reference runtime only with --allow-run |

The normal native build emits x86-64 assembly, not an automatically linked executable. Use the normal assembler/linker, native web CLI, or an external deployment platform separately according to the project. An agent orchestrator may consume these results but retains responsibility for testing, review, and deployment.

### Safety and scope

- Default step budget is 12, configurable 1–64; compiler calls time out after 15 seconds, output capped at 128 KiB.
- Source files are limited to 256 KiB and each JSON request line to 512 KiB.
- Paths must be relative and inside the configured workspace. Absolute paths, directory traversal, and existing symlinks are rejected.
- No arbitrary shell commands, package/network access, automatic git writes, model access, or remote deployment are included.
- Program execution is opt-in via --allow-run for trusted code. This file-boundary guard is **not a security sandbox** for adversarial local processes.
- A failed write_check preserves its source so the next iteration can fix it. Source control can revert undesired modifications.

### Tests

When Node is available, CMake registers the integration test:

    cmake --build build --target noqeri
    ctest --test-dir build --output-on-failure -R noqeri-agent-loop

The test checks readable and JSON errors plus the edit/fail → edit/fix → build → stage sequence. The host deliberately keeps the integration small so ordinary human Noqeri development does not depend on AI.
