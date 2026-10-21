# Noqeri optimizer roadmap and executable qualification

This matrix distinguishes **actual NIR transformations** from design goals. A pass is not considered production-ready merely because the compiler accepts an optimization flag. The Stage-0 C++ NIR path and the self-hosted Stage-2 compiler remain separate qualification boundaries.

| Requested optimization | Current status | Engineering/release gate |
| --- | --- | --- |
| SSA optimization | **Precondition analysis only**: local GVN requires single assignment of each register; no Phi construction | Dominance frontiers, Phi insertion, variable rename, SSA destruction, CFG merge/loop differential tests |
| GVN | **Implemented narrowly**: integer-only pure binary CSE within straight-line SSA functions at O2+ | Native unit regressions and differential -O0/-O2 execution on actual source programs |
| SCCP | **Partial**: block-local constant propagation and literal branch folding; not sparse global dataflow | SSA lattice worklist, executable-edge propagation and Phi refinement |
| LICM | **Not implemented** | Loop detection, dominance, alias/trap proof, preheader synthesis, memory SSA |
| Loop unrolling | **Not implemented** | Cost/size model, overflow and trip-count proofs, nested-loop tests |
| Loop vectorization | **Not implemented** | SIMD IR, data dependence, alias guards, target ISA lowering and tail tests |
| SLP vectorization | **Not implemented** | Isomorphic operation grouping, SIMD legalizer and scheduling |
| Alias analysis | **Not qualified** | Points-to and memory-location model, conservatism for unknown pointers and FFI |
| Escape analysis | **Not qualified** | Proven allocation lifetimes and escape graph across calls/closures |
| Interprocedural optimization | **Very limited**: zero-arg pure constant-return inlining | Call graph, effect summaries, recursion and linkage semantics |
| Register allocation | **Implemented basic linear scan** | CFG live intervals, spills and register-class target correctness |
| Instruction scheduling | **Not implemented** | Target dependency DAG, latency tables, alias and memory-order proofs |
| Strength reduction | **Partial via constant folding**, no algebraic power-of-two rewriting qualified | Signed overflow, fixed-width truncation, shift semantics, trapping behavior |
| Autovectorization | **Not implemented** | Full loop/SLP vectorizers plus benchmarking on supported targets |
| LTO | **Not implemented** | Whole-program IR serialization, symbol resolution and cross-module ABI correctness |
| PGO | **Not implemented** | Instrumentation, profile merge, provenance and reproducible optimized build |
| Target cost models | **Not implemented** | Versioned CPU features, operation costs and target-specific performance tests |

## Validation

```sh
c++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Wno-misleading-indentation \
  -IInclude/noqeri -ICompiler/bootstrap \
  tests/cpp/optimizer_liveness_test.cpp Compiler/optimizer.cpp -o /tmp/noqeri-optimizer
/tmp/noqeri-optimizer
```

**Safety decisions:** GVN requires unique register definitions and no branch/try/inline-asm/volatile instruction in the function; it rewires registers rather than speculatively loading from memory. It only merges operations with proven integer operand types, skipping floating point, unknown types, pointer arithmetic, and calls. This deliberately leaves optimization opportunities on the table until SSA and alias analyses exist. Never discard trapping checks or memory operations to improve a benchmark score.

**Production gate:** native unit tests alone do not establish Stage-2 parity. Require cross-target compiler tests, -O0 vs optimized differential runtime outputs, sanitizers/fuzzing, and measured performance before promising speedups or marking any major pass mature.
