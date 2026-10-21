# Noqeri 1.0.0 production qualification gates

`VERSION=1.0.0` identifies the planned release candidate, **not an already-certified release**.

1. **Language contract:** all public version fields, manifest and self-hosted stage1 exports agree; `node --test tests/version-contract.test.mjs` passes.
2. **Self-hosted compiler:** an executable Noqeri Stage-1 produces Stage-2 and Stage-3 from the exact compiler source closure; compare outputs; independently attest the Stage-1 trust root. Record SHA-256 hashes and the source revision. No C++ semantic fallback in Stage 2.
3. **Feature parity:** Noqeri syntax, type-checking, NIR, native backend, compiled NQRI, packages, formatter/LSP and database facade have baseline and negative fixtures, tested with reference and native runtimes.
4. **Backend and safety:** differential `-O0` versus `-O1/-O2/-O3/-Oz` outputs; bounds, overflow, pointer `unsafe`, atomics and crash/fuzz tests. Don't claim SIMD, PGO, LTO or zero-cost generics without benchmarkable implementations.
5. **Ecosystem:** a genuinely private NoeSQL `.noedb` persistence round-trip through Registry API, signed/checksummed package archives, CLI installs, and website downloads all pass at the same integration source revisions. Registry public mocked tests alone are insufficient.
6. **Platform matrix:** Linux/Windows x86-64 must produce tested runtime artifacts and evidence. macOS/ARM targets are preview, not `supported`, until tests run on real runners.
7. **Evidence:** `scripts/qualify-release.mjs` produces a PASS report for the checkout revision, `Tools/release/release-evidence.mjs` builds checksums and CycloneDX SBOM, and `Tools/release/verify-v1-evidence.mjs` rejects missing evidence, source mismatches, platform gaps and unsigned manifests.
8. **Publication:** only then create a signed GitHub release/tag. Do not treat merely changing `VERSION`, `project.nqon`, or `--version` as publication.

Local verification:
```sh
node --test tests/version-contract.test.mjs tests/release-qualification.test.mjs
node scripts/qualify-release.mjs
node Tools/release/verify-v1-evidence.mjs build/release-qualification/report.json dist/release
```
The latter two **fail closed** unless every required input exists. No evidence is fabricated or implicitly downloaded.
