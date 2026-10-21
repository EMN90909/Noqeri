# Compiler optimization qualification matrix

**Scope:** Noqeri's C++ Stage-0 / native NIR optimizer. This is *not* evidence that the self-hosted Stage-2/Stage-3 backend has equivalent passes. All claims below are limited to their explicitly tested NIR forms. A pass being recognized or costed is not the same as transforming the program.

| Requested pass | Current implementation | What is NOT implemented or qualified |
| --- | --- | --- |
| SSA optimization | Single-definition virtual-register inspection and CFG-dominance verification gate cross-block optimization | SSA construction with Phi nodes, mutable variables, memory SSA and Phi lowering |
| GVN | Basic-block integer `+`, `-`, `*`, `%` value numbering; duplicate NIR operations coalesced when result has no users in other blocks | Global value numbering across branches, loads, volatile memory, pointer calculations, floating NaNs |
| SCCP | Sparse constant discovery on dominance-proven SSA temporaries, Boolean conditional folding, CFG reachability, dead-code pruning | Phi-lattice propagation and fully edge-sensitive sparse dataflow |
| LICM | O3 hoists literals and pure wraparound integer `+`, `-`, `*` proven invariant from a single-entry structured loop preheader | General arithmetic/dataflow LICM, invariant memory loads, aliasing, trapping or multi-entry loops |
| Loop unrolling | O3 bounded 2x structured-loop unrolling; reproduces and rechecks exit condition between body copies; rejects nested/exceptional/external-entry loops; target-cost and verified PGO hints determine use | Larger factors, loop peeling, static trip-count and nested-loop unrolling |
| Loop vectorization | Not implemented | Requires typed vector NIR, legality checks and target SIMD lowering |
| SLP vectorization | Candidate-only `inspectSlpPacks`: groups independent 2x i64 integer ops when target heuristics allow, rejecting data dependencies and unknown targets | No SIMD NIR or machine-vector emission; not a completed vectorization pass |
| Alias analysis | `NirAliasAnalysis` proves distinct nonescaping acyclic `StackAlloc` roots do not alias; all other uncertain uses remain MayAlias | General points-to graph, interprocedural heap aliasing, memory SSA |
| Escape analysis | Tracks direct StackAlloc roots and derivatives, marks values passed to unknown sites / returned as escaping | Allocation replacement, promotion, allocation sinking and heap devirtualization |
| Interprocedural optimization | Bounded (up to 12 instructions) zero-argument pure arithmetic/constant/call evaluation across the program, with duplicate-symbol and external/effect rejection | Parameter specialization, general inlining, recursion and whole-program effect analysis |
| Register allocation | CFG liveness fixpoint extends intervals over backedges/branches; repeated Reg defs unioned; deterministic linear scan with unique spill slots | Graph coloring, split/reload insertion, per-register-class allocation and spill-cost optimization |
| Instruction scheduling | O3 local DAG prioritization for independent, nontrapping integer expressions, plus basic literal sinking, within basic-block effect barriers | Calibrated machine scheduling, explicit register-class hazards and memory dependencies |
| Strength reduction | O2+ `x*0` and `x%1` for wraparound `Int/I64`, preserving precomputed operand effects | Power-of-two division lowering, induction variable arithmetic |
| Autovectorization | Pure integer SLP feasibility analysis and target-gated pack candidates only | No vector-producing pass, SIMD IR, lowering, or executable auto-vectorization yet |
| LTO | Pre-link `combineNirModules` concatenates effect-free imported NIR function modules with duplicate symbol / unsafe initializer rejection; bounded cross-module pure IPO can then run | No `.nqri` object IR serialization/loader, native LTO binary linker, generic symbol resolution or cross-module ABI qualification |
| PGO | Opt-in interpreter instruction counters, fingerprint-bound NQPGO1 profile import/export, hot/cold unroll-cost selection and hard failure on stale profile | No native machine-code instrumentation, cross-run deployment CLI or production profile training/merging |
| Target cost models | Heuristic x86-64/ARM64 integer/branch costs now gate actual loop-unroll decisions; SIMD estimates gate SLP candidate planning; unknown targets reject vector claims | Benchmark-calibrated microarchitecture data, versioned CPU feature detection and target-specific SIMD machine code |

## Native pipeline eligibility

- **O0:** No optimizations.
- **O1:** Existing local constant folding, dead-code elimination and proven safe bounds-check simplification.
- **O2:** O1, dominance-gated sparse constant propagation, branch pruning, conservative local integer GVN and identity strength reduction.
- **O3:** O2, proven integer LICM, guarded 2x loop unrolling, local integer DAG scheduling and literal sinking. Target heuristic and source-verified interpreter PGO feedback control the unroll expansion.
- **Oz:** Conservative algebraic/CFG optimization with code size goal; no speculative loop unroll.
- **Os:** O1-like baseline. Not automatically equivalent to O3.

All passes must preserve Noqeri's source-visible side effects, overflow and trap semantics. Unknown pointer interactions are MayAlias. Volatile/atomic loads, inline assembly, exception edges and unqualified floating-point rearrangements are not removed or moved. A target cost profile in a header does not mean SIMD instructions are emitted.

## Required next engineering phases

1. Implement canonical SSA formation/renaming, Phi nodes, critical-edge splitting and SSA destruction; test mutable register variables at joins and loop latches.
2. Add memory SSA plus alias+escape proofs before global GVN, arithmetic LICM or redundant-load elimination.
3. Define typed vector instructions, lane semantics, alignment and scalar fallback across native backends, then add SLP and loop vectorization with auto-generated differential fixtures.
4. Make target feature detection and cost tables drive pass legality and codegen. Benchmark ARM64, baseline x86_64 and older Intel GPUs independently.
5. Connect restricted pre-link IR combining to real module-loading/linking stages, add whole-program symbol resolution, instrument native machine code, and ship version-verified PGO profile CLI merging; compare profile-directed results against a no-profile baseline.
6. Run O0/O1/O2/O3/Oz differential tests, native sanitizer/fuzzer suites, multithreaded memory tests and CMake release builds on Linux and Windows. Record compile time, executable speed and memory before claiming mature performance.

**Release gate:** native Stage-2 compiler parity, real NoeSQL integration, full platform qualification and performance baselines are not yet proven. Keep these pass statuses explicit in published documentation.

## Phase 3 runtime-bound evidence

- `Compiler/lto.cpp` / `Include/noqeri/lto.hpp`: in-memory NIR module combiner with strict initializer and duplicate-function checks. This works on explicitly supplied NIR modules only, not `.nqri` object archives.
- `Include/noqeri/pgo.hpp`: NQPGO1 interpreter counter collection and fingerprint-checked serialization; `Interpreter::setExecutionProfile` attaches counters during NIR execution. `PassManager(O3, &profile)` uses matching hot/cold evidence exclusively to adjust legal loop unrolling, never to bypass safety analysis.
- `inspectSlpPacks` reports target-cost-gated 2x integer operation packs but emits **no SIMD**. Do not equate pack discovery with vectorization.
- `tests/cpp/optimizer_liveness_test.cpp`, `tests/cpp/lto_ir_test.cpp` and `.github/workflows/optimizer-phase3.yml` check branch exits, observable effects, pure arithmetic hoisting, scheduling, profile mismatch rejection, import validation, and actual O0/O3 program output equality.

**Noqeri Stage-2 self-host optimizer parity is not established** by these Stage-0 C++ NIR tests, and the source still lacks a Phi-capable SSA representation, vector IR and machine SIMD. Those remain hard production-release blockers for a claim that all 17 techniques are complete.
