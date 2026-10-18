# Noqeri 0.7.0 general-purpose release verification — 2026-10-18

## Release decision

**NOT QUALIFIED AS STABLE.** The Noqeri source manifests identify development version 0.7.0. No verified Stage-2 bootstrap, complete native executable, cross-platform test matrix, or complete regression proof is available. A version string or successful merge does not constitute release certification.

**Product scope:** Noqeri is a general-purpose statically typed language. Game development is an optional SDK under `Lib/game/` with separate optional graphics/runtime qualification. A fully functional 3D engine is **not** a prerequisite for language stability, but shipping experimental game APIs must not break the core compiler/runtime.

## Verified repairs

- Repaired a syntax defect in `scripts/qualify-release.mjs`: literal escaped newlines previously prevented the Node script from parsing.
- Reworked general-purpose qualification to run web tests, NoeSQL tests, optional game asset tests, bootstrap, core conformance, historical regression tests, and cross-repository distribution checks. Missing proof is recorded as a failure, not a pass.
- Added `tests/release-qualification.test.mjs` to confirm script syntax, fail-closed operation, and structured output when the trusted seed or suites are missing.
- Fixed optional game OBJ asset validation: rejects concave, non-planar and self-intersecting polygon faces instead of incorrectly triangulating them; validates parser limit values.
- Fixed game input to preserve both button press and button release events in a single frame, with an additional native Noqeri fixture assertion. The native fixture **has not been executed** because a verified compiler/runner is unavailable.

## Executed test evidence

Environment: Linux container, Node.js v22.16.0; a subset of the GitHub source tree was previously reconstructed locally. For the modified JavaScript files, blob hashes of the tested files were compared with GitHub source blobs before commit.

```sh
node --check Tools/game/validate-obj.mjs
node --check scripts/check-game-sdk.mjs
node --check scripts/qualify-release.mjs
node --test tests/game-obj.test.mjs tests/release-qualification.test.mjs
# 8 tests passed, 0 failed

node Tools/game/validate-obj.mjs examples/game_sdk/assets/cube.obj --json
# 8 vertices, 12 triangles, bounds [-1,-1,-1] to [1,1,1]

node scripts/check-game-sdk.mjs --source-only
# PASS asset validator unit/adversarial tests and cube fixture

env -u NOQERI_STAGE0 node scripts/qualify-release.mjs
# exit 1, reporting missing trusted compiler seed and unavailable suites
```

The local subset lacks the complete web/NoeSQL tests and sibling repositories. Those missing suites are **unavailable to this local test**, not evidence that full-repository tests fail. The full GitHub repository could not be cloned because the local worker could not resolve github.com. There is no trustworthy claim of passing Noqeri compilation, general-purpose integration, native game execution or GPU rendering.

## Unresolved stable 0.7.0 blockers

- [ ] Obtain independently trusted `NOQERI_STAGE0` seed; build and prove Stage-1 -> Stage-2 -> rebuilt Stage-2 equivalence.
- [ ] Execute positive/negative grammar, type, memory, ownership, generics, optimizer and native ABI conformance tests.
- [ ] Produce, link and run a native general-purpose application; verify correct external function calls and cleanup.
- [ ] Verify full web, NoeSQL, package-manager, security and existing regression suites on a complete checkout.
- [ ] Verify native `tests/game_foundations.nqr` and investigate compiler errors if any; optional 3D graphics remain experimental.
- [ ] Verify clean installs, release artifacts, signatures and platform-specific execution on every advertised target.
- [ ] Run `node scripts/qualify-release.mjs` with the trusted seed and required sibling projects; retain the output `build/release-qualification/report.json`.
- [ ] Publish stable 0.7.0 only after all release-critical gates pass.

See `Doc/SELF_HOSTING.md`, `Doc/PRODUCTION_READINESS.md`, and `Doc/GAME_DEV_READINESS.md`. Do not remove the archival C++ bootstrap seed without a verified fixed-point compiler rebuild.

## Developer experience

The simplest currently reproducible no-compiler workflow is `node scripts/check-game-sdk.mjs --source-only`, which validates the optional OBJ asset pipeline. It is **not** a usable general Noqeri application development workflow. Time-to-first-compiled-program and fresh-machine setup success cannot be measured until an executable compiler is available.

## Independent GitHub clean-checkout checks on PR #17 (2026-10-18)

This workstream added `.github/workflows/v0.7-release-verification.yml` so a complete GitHub runner checkout tests the repository rather than relying on a partial local reconstruction. The existing `Native database safety` workflow also passed on the PR branch.

The portable web, NoeSQL reference, game asset and tooling suites **passed** on the Ubuntu Node.js 22 runner. The Windows PowerShell build-script parse check **passed**.

The complete historical C++ native CTest suite **still fails**. It initially passed 14/24 tests (10 failed). Repairs to the generic type rewriting, .noe doctor policy, unsafe short-circuit fixture and Pair reference signature improved the observed status; the later clean-run result was 18/24 (6 failed). Failing tests include general library, component, selftest and stress suites. **Do not treat 18/24 as a release pass.**

The CMake legacy C++ compiler **built successfully** and was independently tried as the `NOQERI_STAGE0` candidate. The self-host build failed when checking `Compiler/selfhost/lexer.nqr` at a typed declaration without initializer (`let token: NqToken`). The self-hosted language has zero-init normalization that this historical C++ frontend does not implement. A compiler binary existing is not proof it can compile Noqeri's self-hosted compiler.

Additional bootstrap proof hardening on the branch requires:
- a configured, executed Stage-0 -> Stage-1 build command and nonempty stage1 artifact;
- separate Stage-1, Stage-2 and rebuilt-stage2 artifact paths;
- freshly written output artifacts rather than stale successful results;
- Stage-2 ESM ABI and numeric version exports.

Evidence and logs:
- [Full 0.7 verification workflow](https://github.com/EMN90909/Noqeri/actions/workflows/v0.7-release-verification.yml)
- [Run proving portable and Windows checks, native/compiler blockers](https://github.com/EMN90909/Noqeri/actions/runs/39083185149)
- [Existing native database safety workflow](https://github.com/EMN90909/Noqeri/actions/workflows/native-database-safety.yml)

Remaining critical release work is **implementation and conformance**, not missing checkboxes: implement compatible typed zero-initialization in the Stage-0 migration path, repair existing unsafe-pointer and compiler test failures, prove Stage-2 rebuilding the compiler, and execute general-purpose language correctness, native runtime and distribution gates. Do not publish the stable tag until the whole release process succeeds.
