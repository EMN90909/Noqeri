# Noqeri web-development verification report

Date: 2026-10-17
Scope: current `main` source, Stage-2 CLI, self-host bootstrap, historical browser backend and newly added local web smoke snippets.

## Evidence and limits

- Reviewed `Runtime/stage2-cli.mjs`, `Compiler/selfhost/main.noqeri`, `Compiler/selfhost/nqri_writer.noqeri`, `scripts/build.sh`, `scripts/test.sh`, `Doc/SELF_HOSTING.md`, `examples/web_app.noqeri` and `tests/web_module.noqeri`.
- External execution environment could not clone the repository: GitHub DNS resolution failed. No usable compiled Noqeri binary was available to execute `.noqeri` programs. **No passing compiler execution is claimed.**
- No GitHub Actions were used, added or invoked.

## Findings

| ID | Severity | Status | Finding |
| --- | --- | --- | --- |
| WEB-01 | Critical | Confirmed in source | `Runtime/stage2-cli.mjs` recognizes `check`, `syntax`, `lex-count`, `fingerprint`, package commands, and diagnostics, but not `web`. It responds with usage and exit code 2 for unknown commands, so the current Stage-2 CLI cannot emit browser JavaScript from `.noqeri`. |
| WEB-02 | High | Unverified | `Compiler/selfhost/nqri_writer.noqeri` defines an NQRI portable object format; writing this format is not evidence of runnable ES-module output. Browser module code generation has not been proven end-to-end through Stage 2. |
| WEB-03 | High | Confirmed in source | `scripts/test.sh` calls legacy `run`, `nir`, `web`, `db`, `format`, `build`, `manifest`, `lock` and `targets`. These are not supported in the inspected Stage-2 CLI. The script is not a trustworthy passing Stage-2 test until its scope is separated from the historical compiler. |
| WEB-04 | High | Bootstrap prerequisite | `scripts/build.sh` invokes `$NOQERI_STAGE0 check` and `$NOQERI_STAGE0 web`. A trusted seed with these commands must exist; the build cannot start from source alone on a fresh host. |
| WEB-05 | Medium | Source/docs drift | `README.md` describes a Stage-1 `noqeri-stage1.nqri` output and only the older command subset. Current `scripts/build.sh` actually names `noqeri-stage2.nqri` and the CLI has additional commands. |
| WEB-06 | High | Unverified | DOM host ABI linkage and JavaScript emitter correctness require running emitted modules against a defined browser/host adapter; source declarations alone do not demonstrate this. |
| WEB-07 | High | Unverified | The self-host equivalence gate in `scripts/bootstrap-stage2.mjs` requires configured builds, artifact equivalence, and conformance/hardening suites; there is no observed passing local proof in this audit. |

## Added test fixtures

- `tests/web_smoke/01_exports.noqeri`: named exported functions, strings and calls.
- `tests/web_smoke/02_json.noqeri`: escaped JSON string for HTTP responses.
- `tests/web_smoke/03_dom.noqeri`: browser host ABI call.
- `tests/web_smoke/04_arithmetic.noqeri`: arithmetic correctness.
- `tests/web_smoke/05_invalid_type.noqeri`: negative semantic check.
- `tests/web_smoke/06_invalid_browser_asm.noqeri`: native assembly must not compile to a browser module.
- `scripts/test_web_dev.mjs`: local-only runner that executes the compiler, imports emitted ES modules in Node.js, checks their real return values, and emits PASS/FAIL JSON. It fails when web generation is unsupported instead of inventing a passing result.

## How to run, with no GitHub Actions

From a local clone, after building or providing a **trusted compiler with the capabilities you want to test**:

```sh
NOQERI_BIN=./build/noqeri node scripts/test_web_dev.mjs
```

The current Stage-2 binary is expected to fail the ES-module generation checks until the web command is implemented. To evaluate the historical bootstrap separately, set `NOQERI_BIN` to the historical compiled CLI instead; retain separate results so legacy functionality is not confused with Stage-2 parity.

## Release gate

Do not claim general web-development readiness until `check` passes for the valid snippets, rejects the invalid type, `web` creates importable ES modules, export/JSON/arithmetic/DOM tests execute, unsafe native assembly is rejected for browser compilation, and local bootstrap equivalence/conformance suites are evidenced.

This report is deliberately conservative: review-only findings are not labeled as executed tests.
