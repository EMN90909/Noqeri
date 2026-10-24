# Compiler optimization qualification matrix

**Scope:** Noqeri's C++ Stage-0 / native NIR optimizer. This is *not* evidence that the self-hosted Stage-2/Stage-3 backend has equivalent passes. All claims below are limited to their explicitly tested NIR forms. A pass being recognized or costed is not the same as transforming the program.

| Requested pass | Current implementation | What is NOT implemented or qualified |
| --- | --- | --- |
| SSA optimization | CFG fixed-point scalar Load forwarding for identical incoming registers; O2+ lowers a restricted typed Phi at two-way acyclic branch diamonds into two predecessor-edge virtual register copies, replacing the join Load | Full SSA renaming, dominance-frontier Phi insertion, loop-carried Phi, critical-edge splitting, parallel-copy cycles, general mutable-variable promotion |
| GVN | Basic-block integer `+`, `-`, `*`, `%` value numbering; duplicate NIR operations coalesced when result has no users in other blocks | Global value numbering across branches, loads, volatile memory, pointer calculations, floating NaNs |
| SCCP | Sparse constant discovery on dominance-proven SSA temporaries, Boolean conditional folding, CFG reachability, dead-code pruning | Phi-lattice propagation and fully edge-sensitive sparse dataflow |
| LICM | O3 hoists literals and proven preheader-dominating Int/I64 `+`, `-`, `*` from single-entry structured loops; a capped repetition handles several invariants | General LICM with side effects, trapping division, invariant memory loads, alias-aware hoisting, loop unswitching and full SSA/Phi |
| Loop unrolling | Not implemented | Requires proven trip count, exact overflow semantics, size and target cost gating |
| Loop vectorization | Not implemented | Requires typed vector NIR, legality checks and target SIMD lowering |
| SLP vectorization | Not implemented | Requires pack formation, def-use/alias and instruction selection |
| Alias analysis | `NirAliasAnalysis` propagates `StackAlloc` roots through pointer derivations using an O(N+E) worklist; distinct nonescaping acyclic roots are proven NoAlias and all uncertain uses remain MayAlias | General points-to graph, interprocedural heap aliasing | 
| Memory SSA | One conservative whole-memory version partition with stable instruction write versions, CFG join/backedge Phi records and same-version CSE for typed nonvolatile loads from nonescaping stack roots | Per-object alias-partitioned MemorySSA, MemoryDef/MemoryUse walkers, interprocedural barriers, arbitrary memory-Phi lowering |
| Escape analysis | Tracks direct StackAlloc roots and derivatives, marks values passed to unknown sites / returned as escaping | Allocation replacement, promotion, allocation sinking and heap devirtualization |
| Interprocedural optimization | Exact pure zero-argument constant-return call substitution; externs and unknown bodies retained | Full call-graph specialization, partial inlining, recursion handling and whole-program IPO |
| Register allocation | CFG liveness fixpoint extends intervals over backedges/branches; deterministic linear scan with stack spills and preserved r12-r15. The Linux x86-64 emitter also promotes at most one loop-hot non-address-taken named local into callee-saved RBX, disabled for inline-asm functions. | Graph coloring, split spills, promotion of multiple locals, optimal spill costs, cross-target machine scheduling and native Stage-2 parity |
| Instruction scheduling | O3 moves only integer/Boolean literals within a straight-line basic block toward first use; effects and branch indices unchanged | Dependency-latency DAG, hazard tracking, machine-instruction scheduling |
| Strength reduction | O2+ `x*0` and `x%1` for wraparound `Int/I64`; Stage-0 native x86-64 emits unsigned reciprocal/multiply/correction division for constant 64-bit divisors and shifts for power-of-two constants, with guarded signed reconstruction. | Induction-variable algebra and a generic target-independent magic-number division pass; Stage-2 native parity |
| Autovectorization | Not implemented | No vector-producing pass exists yet |
| LTO | Not implemented for compiled Noqeri modules | Linker IR summary/import, symbol resolution and cross-module IPO |
| PGO | Not implemented | Instrumentation, counter collection, profile validation, hot/cold layout |
| Target cost models | `TargetCostModel` includes conservative heuristic x86_64/ARM64 integer ops and 128-bit candidate SIMD width; unknown targets deny SIMD assumptions | Empirically calibrated throughput/latency models or use by vector/codegen passes |

## Native pipeline eligibility

- **O0:** No optimizations.
- **O1:** Existing local constant folding, dead-code elimination and proven safe bounds-check simplification.
- **O2:** O1, dominance-gated sparse constant propagation, branch pruning, conservative local integer GVN and identity strength reduction. Proven redundant bounds checks with identical operands and guarded unsigned fixed-array indices are removed only when the CFG and mutation proof succeeds.
- **O3:** O2, capped structured literal plus nontrapping invariant Int/I64 addition/subtraction/multiplication LICM, and local literal scheduling for register pressure.
- **Oz:** Conservative algebraic/CFG optimization with code size goal; no speculative loop unroll.
- **Os:** O1-like baseline. Not automatically equivalent to O3.

All passes must preserve Noqeri's source-visible side effects, overflow and trap semantics. Unknown pointer interactions are MayAlias. Volatile/atomic loads, inline assembly, exception edges and unqualified floating-point rearrangements are not removed or moved. A target cost profile in a header does not mean SIMD instructions are emitted.

## Benchmarking native code generation

The Linux x86-64 Stage-0 emitter uses conservative, callee-saved physical
registers for allocated NIR values. It still reserves the original virtual
stack slots; most named mutable locals still live in memory, except for
one eligible loop-hot local promoted to saved RBX. This is a
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

## Native x86-64 improvements on 2026-10-24

- **Typed NIR optimization:** the TypeChecker still runs first, but NIR type
  analysis now runs before optimization, with CFG metadata rebuilt after transformation.
  Integer constant comparisons, division and remainders use the signedness
  of their typed operands rather than always interpreting the bits as i64.
- **Safe array indexing:** the type checker now attaches an actual fixed
  array length to `IndexExpr`; that was previously missing for local fixed
  arrays and was a genuine missing-bounds-check correctness issue.
  O2/O3/Oz remove an explicit `CheckBounds` if either an identical test
  dominates it or an unsigned index is proven smaller than a fixed bound
  by a dominating conditional, with no intervening named-local assignment,
  unknown call, inline assembly, or unchecked memory write.
- **Constant division:** the Stage-0 emitter uses 64-bit reciprocal multiply
  and a single remainder correction for non-power-of-two divisors. Power
  of two divisors use shift/mask. Signed operands are converted through
  their unsigned magnitude and restored according to truncation/remainder
  sign semantics. Division by zero and i64 min / -1 retain hardware trap
  behavior; unproven divisors keep hardware `div`/`idiv`.
- **Machine lowering:** the native emitter fuses adjacent single-use integer
  comparisons with conditional branches, chooses unsigned x86 flags for
  typed unsigned comparisons, sign-extends signed narrow-memory loads,
  avoids unused instruction labels, and shortens positive fixed array
  bounds tests to one unsigned compare. These techniques are not yet in
  the NQRI-VM/self-host Stage-2 backend.
- **Regression and performance evidence:** run
  `python3 tests/native_division_matrix.py` after building
  `build-native/noqeri` to exercise signed/unsigned quotient and remainder
  over multiple integer extremes at O0 and O3. The native-typed-arithmetic
  CI test requires unsigned/narrow results and traps for invalid indexes.
  The same-runner native A/B workflow measures already-sorted traversal,
  dependent modulo, and compiler latency; its raw results are not global
  speed claims.

## Required next engineering phases

1. Implement canonical SSA formation/renaming, Phi nodes, critical-edge splitting and SSA destruction; test mutable register variables at joins and loop latches.
2. Add memory SSA plus alias+escape proofs before global GVN, arithmetic LICM or redundant-load elimination.
3. Define typed vector instructions, lane semantics, alignment and scalar fallback across native backends, then add SLP and loop vectorization with auto-generated differential fixtures.
4. Make target feature detection and cost tables drive pass legality and codegen. Benchmark ARM64, baseline x86_64 and older Intel GPUs independently.
5. Implement LTO IR summaries and whole-program symbol linking, profile instrumentation and checked PGO metadata; compare PGO to a no-profile baseline.
6. Run O0/O1/O2/O3/Oz differential tests, native sanitizer/fuzzer suites, multithreaded memory tests and CMake release builds on Linux and Windows. Record compile time, executable speed and memory before claiming mature performance.

**Release gate:** native Stage-2 compiler parity, real NoeSQL integration, full platform qualification and performance baselines are not yet proven. Keep these pass statuses explicit in published documentation.

## Issue #48 native optimization status (October 24, 2026)

- **Correctness regression fixed:** O3 no longer folds signed i64 minimum
  divided/remainder by -1 into a nontrapping constant. The native qualification
  suite now expects SIGFPE at both O0 and O3 for overflow and zero division.
- **Bounds proof hardening:** a guarded index cannot inherit stale range
  information from a scalar load preceding a named-local mutation.
- **Loop hoisting:** the Stage-0 pass now covers nontrapping invariant
  64-bit integer `+`, `-`, `*` as well as literals. This is not general
  SSA/Phi construction or arbitrary code motion.
- **Compiler analyses:** loop candidate counting and stack-pointer root
  propagation use linear-time scans/worklists instead of repeated scans.
- **Native measurements:** hot-local RBX promotion passed ABI/native
  correctness tests but its same-runner already-sorted insertion-sort speedup
  was ~1.001x, essentially unchanged. The scoped O3 LICM branch measured
  ~1.263x on dependent modulo and ~0.894x on the already-sorted traversal;
  neither supports a blanket speedup claim.
- **Better benchmarking:** a fresh deterministic seeded data fill every sorting
  round adds a reshuffled-sorting A/B lane with checksum 13104300. A test-only
  change yielded median 21.932 vs 21.911 ms, within practical measurement noise.

**Still pending:** full SSA and Phi creation/destruction, memory SSA,
induction/range proofs beyond guarded scalar cases, generalized
unrolling/vectorization, full typed integer-width semantics and
native Stage-2/Stage-3 self-hosted compiler parity. Issue #48 remains open.

## CFG scalar promotion qualification (October 24, 2026)

PR #66 adds conservative fixed-point scalar facts at control-flow joins.
A local Load is forwarded only when all incoming reachable paths agree on
one dominating register with an identical supported integer/Boolean type.
An address-taken or volatile variable, side-effect barrier, loop-carried
mutation or divergent predecessor retains the memory Load. This is not a
replacement for explicit Phi instruction lowering or memory SSA.

The same-runner native benchmark with three million iterations through a
branch diamond produced median runtime **5.389196 ms** (previous main) versus
**5.117053 ms** (candidate), approximately **1.053x** for this one workload.
The benchmark and Linux/Windows bootstrap qualification passed on PR #66.
Those timings do not establish a cross-workload or cross-target speedup.

## Additional Issue #48 compiler work (October 24, 2026)

- **PR #68:** sparse integer constant propagation uses a def-use worklist
  rather than rescanning whole NIR bodies until no constant changes. This is
  *not* complete edge-sensitive SCCP or Phi-lattice propagation.
- **PR #70:** guarded fixed-array bounds-check removal indexes unknown memory
  writes by prefix counts and named-local Store positions, replacing repeated
  instruction rescans. Stale-value, may-alias, signed nonnegativity and
  loop-backedge safety qualifications remain intact.
- **PR #72:** transitive NIR DCE register liveness uses a dependency worklist,
  conservatively combining operands from repeated definitions. A 2,048-link
  live chain and 512-link dead chain are regression-tested.
- **PR #71:** Stage-0 native arithmetic now normalizes u8/i8/u16/i16
  and u32/i32 signedness and wrapping at operation boundaries, matched by
  typed optimizer constant folding. The TypeChecker's declared array,
  field and pointer element types now reach the NIR LoadMemory instruction:
  generic StackAlloc byte pointers no longer mislabel 64-bit indexed loads
  as u8. O0 and O3 narrow-integer and 64-bit matrix results agree with
  expected outputs; Linux/Windows bootstrap and independent GCC/Clang
  Stage-1 seed reproducibility passed. This is not cross-target or
  self-hosted Stage-2 native instruction-selection parity.

These changes reduce repeated compiler analyses but provide no universal
end-to-end compiler speedup guarantee. Neither full SSA/Phi, general loop
induction/range proof, auto-vectorization, nor native compiler self-hosting
is complete.

## Issue #48 engineering qualification: additional foundations

The following changes are merged and qualified by the native compiler and
Linux/Windows NQRI bootstrap workflows. They do **not** imply that the entire
compiler optimization roadmap is complete.

- PR #75 adds a conservative whole-memory version graph with explicit memory
  join Phi records. Only nonvolatile repeated loads from proved nonescaping
  stack roots may be reused when the same pointer, type, width, incoming
  memory version and CFG dominance are proven. Unknown writes, calls, atomic
  operations, fences, volatile accesses and inline assembly invalidate or
  disable this proof. This is not per-object MemorySSA and does not itself
  lower scalar Phi copies.
- PR #76 adds opt-in fail-closed release performance gate arguments to
  scripts/perf-gate.mjs. Required workload IDs, same-host profiles, raw
  samples, independently recomputed median/p95, native AOT provenance,
  deterministic output and two release regressions are verified. This
  provides evidence policy, not measured parity with another language.
- PR #77 removes more redundant bounds checks when a guarded index plus
  a positive constant is provably in range without overflow. It also
  fixes the unsafe stale-snapshot case where the checked index was read
  before a mutation but the guard used a newer value. This is a scoped
  affine range proof, not general symbolic induction analysis.
- PR #78 inserts a typed scalar Phi candidate for a strictly eligible
  two-way acyclic branch join, lowers it into predecessor-edge NIR Cast
  copies and removes the redundant join Load. Native O0/O3 tests select
  both incoming values correctly. It refuses loops, exceptional flows,
  address-taken/volatile variables, critical edges and unsupported types.
  This is not complete general scalar SSA or edge copy resolution.

Still missing or unqualified: general scalar SSA renaming and Phi
insertion/destruction (including critical edges and loops), per-object memory
SSA and interprocedural alias analysis, induction trip-count proofs, general
safe unrolling, typed vector NIR and SIMD machine lowering, full integer
parity across native, interpreter and NQRI, native self-hosted Stage-2/Stage-3
executables, and broad measured cross-language performance comparisons.

## Differential Stage-0 backend integer parity

The native typed-arithmetic CI suite directly compares the C++ Stage-0
reference interpreter against native x86-64 AOT output, at both O0 and O3, for
unsigned wide division and comparison, narrow signed and unsigned overflow,
branch Phi selection and guarded loops. Neither backend may silently disagree
on printed observable output. This is qualification of two Stage-0 execution
paths on the cited fixtures. NQRI VM integer-width parity, other target ABIs
and exhaustive integer edge-case fuzzing remain unqualified.
