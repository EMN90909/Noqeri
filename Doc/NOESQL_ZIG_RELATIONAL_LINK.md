# Noqeri-owned NoeSQL → Zig relational module

The standalone NoeSQL repository provides a Linux-only experimental Zig pager with NQDB0002 file format and a v2 C ABI. This repository now mirrors the typed Noqeri source-level binding and first single-row execution slice under Database/noesql_zig_relational.noqeri. Its implementation calls Noqeri's authenticated typed API validator and relational executor, then the native Zig page getter/stager/commit, and uses NROW row encoding for a bounded numeric (int,bool) table.

**This is not a built Noqeri-native executable.** The self-host compiler's main branch has a Stage-2 NIR pipeline, but no proven trusted compiler seed and runnable executable object/linker pipeline. The new tests/noesql_zig_relational.noqeri is therefore a source-only fixture. It must not be counted as a passed native compilation, benchmark, crash-recovery suite or full SQL frontend.

Runtime tests and release qualification remain in the NoeSQL repo; native interfaces require linking the Zig pager library from that repo. The correct next milestone is a verifiable native code-generation and linking path with machine-readable source and compiler provenance, independently executed unit/integration/differential tests and platform-specific binaries.
