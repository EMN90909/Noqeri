# Release process

A Noqeri release is evidence-backed. A tag, archive upload or green marketing page is not sufficient by itself.

## 1. Freeze the candidate

Record the exact source commit, compiler/bootstrap provenance, supported language/edition/ABI/registry protocol, and supported target matrix. Release work may run on local or self-managed machines; GitHub Actions is not required. Whatever executes a release gate must retain enough command/output metadata to reproduce the result.

Every gate is reported as one of: `passed`, `failed`, `unsupported`, or `not-run`. Do not turn an unavailable runner into a pass.

## 2. Correctness and safety gates

Before packaging, run the native/compiler test suite, practical project corpus, package compatibility checks, deterministic fuzz targets, checked-overflow tests, memory-sanitizer mode and race mode on platforms that support the relevant instrumentation. A sanitizer/race finding is triaged before release rather than hidden by retrying.

Run the current active practical corpus locally:

```sh
node Tools/corpus/run.mjs --noqeri=build/noqeri
```

A release advertised as satisfying the complete practical corpus must use `--require-complete`; pending application classes then fail the gate rather than being counted as passes.

Noqeri now contains a language-owned race event/instrumentation model in `Compiler/selfhost/race.noqeri` and a bounded Noqeri runtime race core in `Runtime/race.noqeri`. Until the native instrumentation path is proven end-to-end on the release toolchain, `noqeri test --race` also relies on the ThreadSanitizer-backed host build as execution evidence. Do not describe the native detector as fully integrated merely because its compiler/runtime components exist.

Run the dependency/security audit with advisory data available. `unknown` advisory status is not clean status. Follow `InternalDocs/SECURITY_RESPONSE.md` for embargoed vulnerabilities.

## 3. Performance evidence

Run the benchmark suite using `Benchmarks/METHODOLOGY.md`, archive raw samples and compare the candidate against an accepted same-host baseline with `scripts/perf-gate.mjs`. If an important workload exceeds its threshold, the performance gate fails until the regression is understood and explicitly resolved.

Do not publish “faster than Rust/Zig/C/C++/Go” from a microbenchmark or from an unmatched machine. Comparative claims must link to the exact reproducible result bundle that supports them.

## 4. Reproducibility

Run `scripts/reproducible-build.mjs` on release builders. Record byte equality when achieved; if a platform is only semantically reproducible, document the differing bytes and why they are non-semantic. Reproducibility failures are release-engineering defects, not numbers to omit.

## 5. Required official artifacts

A complete cross-platform release is expected to contain named artifacts for:

- Windows x86-64;
- Linux x86-64;
- Linux ARM64/aarch64;
- macOS x86-64;
- macOS ARM64/aarch64.

Additional supported architectures may be added without weakening these entries. If an advertised platform is not built, the release is incomplete for that advertised matrix.

## 6. Checksums, signatures and SBOM

Place final immutable artifacts in one directory and run:

```sh
NOQERI_SOURCE_REVISION=<commit> \
SOURCE_DATE_EPOCH=<epoch> \
node Tools/release/release-evidence.mjs dist/release \
  --version=<version> --verify-matrix --sign
```

The tool creates SHA-256 `checksums.txt`, `release-manifest.json`, and a CycloneDX `sbom.cdx.json`. `--sign` creates an armored detached GPG signature for the checksum file and fails if signing cannot be completed. `NOQERI_RELEASE_GPG_KEY` may select the signing key.

The tool never manufactures a signature or silently calls an unsigned release signed. Keep signing keys outside the repository and release logs.

## 7. Publish and verify

After upload, download every public artifact through the user-facing distribution path and verify SHA-256 plus signature. Verify installer/archive startup on its named OS/architecture, `noqeri --version`, a first program, package install/offline path, and at least one real corpus project.

Publish the manifest, checksums, signature, SBOM, source revision, supported-version statement, known limitations, performance methodology/results, security contact, compatibility evidence and corpus evidence together. A release becomes `stable` because these support obligations are met, not because its version string says stable.
