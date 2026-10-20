# Security policy

Security fixes target `main` while noqeri is on the 1.0 production track.

Use GitHub private vulnerability reporting when enabled. Include the affected command, platform, compiler build information, a minimal `.noqeri` reproducer and the security impact.

High-priority reports include compiler memory-safety failures, malicious-source crashes with security impact, native-backend code-generation issues that violate language semantics, package/path traversal, unsafe LSP input handling and CI/release-chain compromise.
