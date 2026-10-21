# Compiler optimization qualification matrix

This is an implementation status matrix, **not** a promise of mature optimizations.

| Optimization | Existing implementation/qualification |
| --- | --- |
| Constant propagation/folding | Basic block-local constant folding in `Compiler/optimizer.cpp`; control-flow joins conservatively clear facts |
| Dead-code elimination | Register liveness elimination for side-effect-free instructions; control flow preserved |
| Bounds-check elimination | Only checks with proven compile-time in-range integer index/length are eliminated |
| Register allocation | Linear-scan intervals with spill slots in `Compiler/register_alloc.cpp`; not a verified global allocator |
| Inlining | Not qualified |
| Loop optimization | Not qualified |
| SIMD/vectorization | Not qualified |
| Escape analysis | Not qualified |
| Alias analysis | Not qualified |
| Instruction selection | Architecture-dependent backend exists; optimal instruction selection not qualified |
| Allocation elimination | Not qualified |
| Link-time optimization | Not qualified |
| Profile-guided optimization | Not qualified |
| Efficient calling conventions | Target contracts exist; no comparative ABI/call-overhead qualification |
| Zero-cost generic specialization | Not qualified |

Before enabling advanced passes, require control-flow aware SSA/def-use provenance, dominance and alias models, overflow semantics, memory effects and exceptional control-flow preservation. Add golden NIR snapshots, randomized differential tests against `-O0`, native executable tests at `-O1/-O2/-O3/-Oz`, architecture-specific register pressure suites and stable runtime/compile-time benchmarks. Never remove checks based on profiling alone, and never move memory operations across unknown effects.

**Release gate:** Noqeri↔NoeSQL native Stage-2 E2E remains unverified, so claims of mature native backend performance are prohibited.
