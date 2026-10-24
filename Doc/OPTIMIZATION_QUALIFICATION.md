# Compiler optimization qualification matrix

**Scope:** Noqeri's C++ Stage-0 / native NIR optimizer. This is *not* evidence that the self-hosted Stage-2/Stage-3 backend has equivalent passes. All claims below are limited to their explicitly tested NIR forms. A pass being recognized or costed is not the same as transforming the program.

| Requested pass | Current implementation | What is NOT implemented or qualified |
| --- | --- | --- |
| SSA optimization | Single-definition virtual-register inspection and CFG-dominance verification gate cross-block optimization | SSA construction with Phi nodes, mutable variables, memory SSA and Phi lowering |
| GVN | Basic-block integer `+`, `-`, `*`, `%` value numbering; duplicate NIR operations coalesced when result has no users in other blocks | Global value numbering across branches, loads, volatile memory, pointer calculations, floating NaNs |
| SCCP | Sparse constant discovery on dominance-proven SSA temporaries, Boolean conditional folding, CFG reachability, dead-code pruning | Phi-lattice propagation and fully edge-sensitive sparse dataflow |
| LICM | Hoists pure integer/boolean literals from single-entry, single-exit structured loop bodies at O3 | Hoisting arithmetic, invariant memory loads or operations which could trap |
| Loop unrolling | Not implemented | Requires proven trip count, exact overflow semantics, size and target cost gating |
| Loop vectorization | Not implemented | Requires typed vector NIR, legality checks and target SIMD lowering |
| SLP vectorization | Not implemented | Requires pack formation, def-use/alias and instruction selection |
| Alias analysis | `NirAliasAnalysis` proves distinct nonescaping acyclic `StackAlloc` roots do not alias; all other uncertain uses remain MayAlias | General points-to graph, interprocedural heap aliasing, memory SSA |
| Escape analysis | Tracks direct StackAlloc roots and derivatives, marks values passed to unknown sites / returned as escaping | Allocation replacement, promotion, allocation sinking and heap devirtualization |
| Interprocedural optimization | Exact pure zero-argument constant-return call substitution; externs and unknown bodies retained | Full call-graph specialization, partial inlining, recursion handling and whole-program IPO |
| Register allocation | CFG liveness fixpoint extends intervals over backedges/branches; repeated Reg defs unioned; deterministic linear scan with unique spill slots. The Linux x86-64 Stage-0 emitter consumes the plan for eligible virtual registers using preserved r12-r15, with stack fallback/spills and an inline-assembly guard. | Graph coloring, interval splitting, optimal spill costs, named-local register promotion, full machine scheduling, and native Stage-2 parity |
| Instruction scheduling | O3 moves only integer/Boolean literals within a straight-line basic block toward first use; effects and branch indices unchanged | Dependency-latency DAG, hazard tracking, machine-instruction scheduling |
| Strength reduction | O2+ `x*0` and `x%1` for wraparound `Int/I64`, preserving precomputed operand effects | Power-of-two division lowering, induction variable arithmetic |
| Autovectorization | Not implemented | No vector-producing pass exists yet |
| LTO | Not implemented for compiled Noqeri modules | Linker IR summary/import, symbol resolution and cross-module IPO |
| PGO | Not implemented | Instrumentation, counter collection, profile validation, hot/cold layout |
| Target cost models | `TargetCostModel` includes conservative heuristic x86_64/ARM64 integer ops and 128-bit candidate SIMD width; unknown targets deny SIMD assumptions | Empirically calibrated throughput/latency models or use by vector/codegen passes |

## Native pipeline eligibility

- **O0:** No optimizations.
- **O1:** Existing local constant folding, dead-code elimination and proven safe bounds-check simplification.
- **O2:** O1, dominance-gated sparse constant propagation, branch pruning, conservative local integer GVN and identity strength reduction.
- **O3:** O2, structured literal-only LICM and pure local literal scheduling for lower register pressure.
- **Oz:** Conservative algebraic/CFG optimization with code size goal; no speculative loop unroll.
- **Os:** O1-like baseline. Not automatically equivalent to O3.

All passes must preserve Noqeri's source-visible side effects, overflow and trap semantics. Unknown pointer interactions are MayAlias. Volatile/atomic loads, inline assembly, exception edges and unqualified floating-point rearrangements are not removed or moved. A target cost profile in a header does not mean SIMD instructions are emitted.

## Benchmarking native code generation

The Linux x86-64 Stage-0 emitter uses conservative, callee-saved physical
registers for allocated NIR values. It still reserves the original virtual
stack slots, and named mutable locals still live in memory. This is a
first-stage improvement, **not** proof of globally optimal register
allocation or elimination of redundant array bounds checks.

For assembly inspection run
`node scripts/native-build.mjs Benchmarks/workloads/sorting.noqeri build/benchmarks/sorting --compiler=build/noqeri --opt=-O3 --keep-asm`.
The generated `.s` file is preserved beside the executable, and its
SHA-256 is recorded in the native provenance JSON. For fail-closed benchmark
coverage, pass `--require-workloads=noqeri:sorting` to
`scripts/benchmark-suite.mjs`, using a complete local commands file.
That argument ensures the named result is *measured*, not simply configured.

The GitHub Native AOT performance A/B workflow builds both baseline and
candidate Stage-0 compilers under identical settings, compares matching
standalone workloads on one runner, and uploads raw samples. Comparisons must
not be presented as self-hosted Stage-2 performance.

## Required next engineering phases

1. Implement canonical SSA formation/renaming, Phi nodes, critical-edge splitting and SSA destruction; test mutable register variables at joins and loop latches.
2. Add memory SSA plus alias+escape proofs before global GVN, arithmetic LICM or redundant-load elimination.
3. Define typed vector instructions, lane semantics, alignment and scalar fallback across native backends, then add SLP and loop vectorization with auto-generated differential fixtures.
4. Make target feature detection and cost tables drive pass legality and codegen. Benchmark ARM64, baseline x86_64 and older Intel GPUs independently.
5. Implement LTO IR summaries and whole-program symbol linking, profile instrumentation and checked PGO metadata; compare PGO to a no-profile baseline.
6. Run O0/O1/O2/O3/Oz differential tests, native sanitizer/fuzzer suites, multithreaded memory tests and CMake release builds on Linux and Windows. Record compile time, executable speed and memory before claiming mature performance.

**Release gate:** native Stage-2 compiler parity, real NoeSQL integration, full platform qualification and performance baselines are not yet proven. Keep these pass statuses explicit in published documentation.
