# Noqeri 1.0.0 platform support matrix

This table is an implementation/qualification roadmap. It does **not** assert stable support merely because the compiler has a target contract or the CI bootstrap once completed.

| Target | Compiler/bootstrap evidence | Native executable/runtime | Release tier |
| --- | --- | --- | --- |
| Linux x86-64 | Native self-host Linux workflow has passed in integration CI | Full v1 conformance/evidence pending | Candidate |
| Windows x86-64 | Native self-host Windows workflow has passed in integration CI | Full v1 conformance/evidence pending | Candidate |
| Linux ARM64 | Target adapter exists; dedicated runner evidence missing | Pending | Preview |
| macOS x86-64 | Target adapter exists; dedicated runner evidence missing | Pending | Preview |
| macOS ARM64 | Target adapter exists; dedicated runner evidence missing | Pending | Preview |
| Windows ARM64 | Target adapter exists; dedicated runner evidence missing | Pending | Preview |

A platform moves from Candidate to Supported only when the same source commit has verified bootstrap provenance, compiler conformance, native artifacts, ABI tests, security checks and artifact digests. A single cross-compiled binary is not platform runtime evidence. A build-only CI job does not qualify a platform.

Machine-readable policy: `release/platforms-v1.json`. Linux/Windows x86-64 are required candidates for the first 1.0.0 release; all others stay explicitly preview until independently exercised.

Release packaging must not use a missing-platform skip or a fake test report as a substitute for actual verification.
