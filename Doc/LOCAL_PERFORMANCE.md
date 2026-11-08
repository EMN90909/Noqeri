# Noqeri performance: local qualification (no GitHub Actions)

The Stage-0 native x86-64 AOT compiler and owned-collections runtime use
**deterministic ownership and explicit disposal**, not a tracing garbage
collector. The runtime changes here optimize allocation and copying on the
existing owned-list path; they do not introduce a GC, nor a generational or
concurrent collector.

## Changes in the performance branch

- Exact magic-multiply division/remainder for known nonzero integer divisors,
  while preserving divide-by-zero and signed overflow traps.
- Direct register-to-register / register-to-memory scalar copies, and direct
  full-width indexed native loads/stores, to reduce scratch-register traffic.
- Dense NIR register-use/definition counters and a lighter register-allocation
  bookkeeping path (these primarily affect compilation throughput).
- Hash-based lookup for owned-collection handles, and fewer redundant lookups
  when inserting map/set entries.
- Bulk relocation of **plain-copy** list elements during growth. Nontrivial
  elements still use clone/drop callbacks with rollback on failed clones.
- Exact maximum list capacity when a doubling allocation would exceed the
  byte limit, avoiding a premature out-of-capacity result.

## Local-only correctness qualification

On Linux x86-64 with CMake, a C++17 compiler, Python 3, and Node installed:

```sh
bash scripts/local-perf-qualify.sh
```

This runs CTest-owned-collection and register-allocation tests plus native
standalone -O0/-O3 arithmetic, trap, and direct-copy regressions. It does not
start a remote service, trigger workflows, or require GitHub Actions.

## Measure actual speed

For an independent C/Go/Noqeri comparison on the **same machine**, use the
existing `scripts/native-crosslang-compare.py` after building the compiler
as `build-compare/noqeri`, and record the full raw JSON results. Do not
compare this build with benchmarks recorded on different CPUs or runners.

```sh
cmake -S . -B build-compare -DCMAKE_BUILD_TYPE=Release
cmake --build build-compare --target noqeri --parallel 2
python3 scripts/native-crosslang-compare.py
```

The runtime comparison uses the native-AOT executable, not the interpreter or
NQRI VM. These focused code-generation changes do not make Noqeri universally
as fast as C. Workloads involving nested array loops and register spilling
still need evidence-driven optimization.

## Qualification status

The performance work was committed without running GitHub Actions. Offline
algorithm checks and a shell syntax check are not substitutes for building
the full compiler, running the native tests, or capturing same-machine
benchmark results. Until local qualification has been executed successfully,
treat the work as **unqualified** for production or release performance claims.

## 2026-11-06: register pressure, NIR SSA/Phi, and bounds follow-on

This update is **a targeted optimization pass, not complete general SSA or
automatic SIMD vectorization**:

- **Register pressure:** linear-scan eviction ranks virtual registers by
  their weighted uses inside natural CFG backedges. Hot loop-carried values
  should no longer lose their register to shorter-lived cold values merely
  because the cold interval ends first. The original conservative live-in/
  live-out analysis and unique virtual-register intervals remain in force.
- **Def-use construction:** dense virtual-register programs use a compact
  adjacency array in the transitive dead-code elimination worklist, with a
  sparse fallback for invalid/huge register ranges. All dependencies from
  multiple definitions remain live until full SSA is implemented.
- **Phi copy lowering:** same-type 64-bit scalars, floats, and Booleans use
  direct machine copies instead of unnecessary RAX round trips. Narrow
  scalar conversions retain their original normalization.
- **Fixed array checks:** native x86-64 code can CMP an allocated register
  or memory index directly against a proven fixed nonnegative length. Dynamic
  slice bounds still receive signed-negative and upper-bound checks.
- **Experimental packed SSE2:** where both packed 64-bit arithmetic lanes
  use an identical RHS, broadcast it once. Prevent packing across an
  intermediate definition of any required input. Existing opt-in behavior
  remains: `NOQERI_EXPERIMENTAL_SIMD=1`. This does NOT establish a
  profitable or general loop-vectorization pass.

A new native bounds emitter check and a local O0/O3 execution regression
cover fixed-array guard success, OOB traps, index mutation, and scalar Phi
outcomes. The allocator unit test now covers a hot loop value facing an
overlapping cold interval. Existing optimizer liveness tests include an
additional long-chain workload exercising dense transitive dependencies.

### Reproduce local results

```sh
bash scripts/local-perf-qualify.sh
```

The runner builds the compiler and native runtime on Linux x86-64, runs
the correctness tests, then writes three evidence files:

- `build-local-perf/allocation-profile.json` — explicit owned collection
  create/grow/read/free paths and thread contention.
- `build-local-perf/compiler-profile.json` — native Stage-0 compiler
  latency with raw samples for a 501-function source.
- `build-local-perf/c-vs-noqeri.json` — verified standalone native-AOT
  versus optimized C on modulo, 16×16 matrix and reshuffled sorting.

For meaningful A/B on the **same physical machine**, retain a separately
built baseline compiler and baseline allocation benchmark executable,
and use `--baseline` with `scripts/local-compiler-profile.py` and
`scripts/local-allocation-profile.py`. No baseline speeds are fabricated
when a baseline binary is not provided.

**Qualification:** the full source checkout is not mounted in the current
coding environment and the external GitHub host cannot be cloned here.
Consequently, this implementation has not passed the complete build/CTest,
native execution, sanitizers, or any same-host comparison with C. Those are
required before release claims or default-enabling additional vectorization.

## 2026-11-06: CFG-wide scalar SSA and automatic stack-loop SSE2

This iteration extends the compiler beyond the earlier single-diamond and
single-loop Phi patterns. It implements a conservative **O3 CFG-wide scalar
mem2reg** pass using stable reaching Store versions and executable predecessor
Phi copies. It can promote multiple scalar locals through nested branches,
joins and loop backedges in one function. It refuses to rewrite uninitialized,
address-taken, volatile, incompatible-type, exceptional, or ambiguous paths.
Multiple independent Phi moves now skip redundant temporary staging; cycles
still preserve their original simultaneous-copy semantics.

The native x86-64 backend also uses direct r/m64 operands for integer
arithmetic/comparisons, including fused compare/conditional-branch lowering,
rather than unnecessarily staging the RHS in R10. Earlier constant-immediate
and division fast paths are preserved.

**Automatic loop SIMD:** the backend now recognizes repeated independent
2 x 64-bit contiguous **private stack** load/add-or-sub/store groups. When
four or more eligible groups share a provably invariant scalar RHS, it emits
SSE2 packed updates using an XMM5 broadcast. It rejects unknown pointers,
escapes, volatile accesses, intervening opaque calls, branches into the
six-instruction group, and non-dominating broadcasts. This precise pattern
is enabled by default for Linux x86-64 native-AOT output. Use
\`NOQERI_NATIVE_LOOP_SIMD=0\` when invoking the compiler to disable it for an
independent scalar comparison. The unrelated, higher-overhead SLP experiment
remains opt-in through \`NOQERI_EXPERIMENTAL_SIMD=1\`.

### Local-only qualification (GitHub Actions not required)

\`\`\`sh
bash scripts/local-perf-qualify.sh
\`\`\`

This also generates:

- \`build-local-perf/auto-loop-simd.json\`: 25 interleaved native samples
  comparing scalar and automatic SSE2 on 32 independent private stack pairs.
- \`build-local-perf/c-vs-noqeri.json\`: same-runner C and Noqeri AOT
  comparison with verified output and raw samples.
- \`build-local-perf/compiler-profile.json\`: Stage-0 compilation latency.
- \`build-local-perf/allocation-profile.json\`: owned collection API costs.

Additional O0/O3 native checks cover multiple loop-carried variables, nested
loops, branches, and bounds-trap parity.

**Scope and claims:** this is NOT complete program-wide SSA across all
aggregate/aliasing/exceptional constructs; it is not full loop
auto-vectorization of arbitrary arrays or runtime-sized loops. It does not
implement a tracing garbage collector: Noqeri continues to use its explicit
owned-value allocation and cleanup APIs. C performance parity cannot be
asserted without executing the above same-host benchmarks and comparing the
actual output and raw timing evidence. No GitHub Actions were used to test
this change. A full build and native execution remain unverified where the
repository cannot be cloned into the local build environment.

## Sorting and matrix hot-path optimization (2026-11-06)

Measured baseline supplied by the user: Go sorting 6.1 ms, Noqeri sorting
27.9 ms. **These are user-supplied figures, not results reproduced in this
session. No new speedup is claimed until the local A/B report exists.**

The two comparison workloads remain unchanged:

- `sorting_reshuffled.noqeri`: 200 independently seeded, 256-element
  insertion-sort rounds, checksum `13104300`. This deliberately matches
  `Benchmarks/comparison/workloads.go` and `.c`; it is *not* replaced by
  a faster algorithm to inflate a benchmark score.
- `matrix.noqeri`: 16x16 integer matrix multiplication, with the same
  200 passes used by Go/C in comparison runs, verified outputs
  `29971` and `28448`.

### Native compiler optimizations

1. Prove an aggregate is a fixed stack object initialized by one
   unconditional named binding. At 64-bit indexed loads and stores, emit
   an RBP-relative indexed memory operand directly. Rebound or
   address-taken bindings retain generic pointer addressing; no bounds
   check is removed by this transform.
2. For all other fused 64-bit indexed memory accesses, use already
   allocated base/index registers rather than staging both through RAX
   and R11. Keep scalar-width and unfused cases on the original path.
3. Place bounds and null traps out of the normal loop fallthrough path:
   hot checks use `jae`/`jz` to an out-of-line `ud2` island.
   Invalid indices (including index == length) still trap.
4. Replace immediate positive power-of-two integer multiplication
   (`row * 16`) with a bitwise shift. Preserve the existing integer
   result-width normalization.

5. For eligible 64-bit integer comparisons, fold a single-use
   array `LoadMemory` into the subsequent compare-and-branch. Only a short
   sequence of nonvolatile named-local loads, constants or NOPs may appear
   between them. Reject floating-point values, stores, control-flow entries
   and other operations that could change source-order behavior.

These are general x86-64 native lowering optimizations, not hard-coded
identification of a benchmark program or function name.

### Sorting API

`Lib/std/sort.noqeri` now provides `nqSortI64` and
`nqSortDescendingI64`: in-place worst-case O(n log n) heap sorting,
with the established insertion sort used for arrays of 24 or fewer
elements. The heap-sort API is **not stable** (equal keys may reorder);
`nqInsertionSortI64` retains its original stable semantics.
No tracing collector or hidden heap allocation is required.

### Qualification

On Linux x86-64, after checking out this commit:

```sh
cmake -S . -B build-local-perf -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-local-perf --target noqeri noqeri-fixed-stack-index-test --parallel 2
python3 tests/native_fixed_stack_index_local.py --emitter build-local-perf/noqeri-fixed-stack-index-test
python3 tests/native_sort_heap_local.py --compiler build-local-perf/noqeri
python3 scripts/local-sorting-matrix-ab.py --candidate build-local-perf/noqeri
```

For a stronger **before/after** comparison, build the old and new
compilers separately on the *same host*, then supply the old path with
`--baseline /path/to/old/noqeri` in the final command. The report
`build-local-perf/sorting-matrix-ab.json` records checked output, raw
timing samples, medians, p95, Go/C versions, CPU, and compiler hashes.

`bash scripts/local-perf-qualify.sh` includes these regressions and
runs the Go comparison when `go` is available. No GitHub Actions,
remote testing, or release deployment are necessary.

**Status:** full Noqeri native compilation and same-machine C/Go
benchmarks have not been executed in this environment (the repository
cannot be cloned by the local build runtime). Independent algorithm
and assembler spot checks are not substitutes for these tests.

## 2026-11-08: dynamic unsigned bounds and arithmetic-checksum comparison

The user-supplied timings motivating this change were: reshuffled sort
Noqeri 34.080 ms vs C 4.574 ms, repeated stable sort 4.450 ms vs C 1.368 ms,
matrix 5.585 ms vs C 1.167 ms, and arithmetic checksum 28.730 ms vs C
22.921 ms. **These were not reproduced by this patch**; there is no
measured faster-than-C claim.

The Stage-0 x86-64 AOT emitter now uses a single unsigned CMP/JAE for
runtime-sized slice bounds when the index register has a proven unsigned
type. When the index is signed or untyped, both the negative-index trap and
the upper-bound trap remain. The fast path handles the x86 prohibition on
memory-to-memory CMP by loading one operand into RAX only when necessary.
No bounds checking is disabled. Two heavily reused non-address-taken named
locals may also occupy RBX and R12; register assignment reserves those
physical registers so SSA temporaries cannot alias them. These changes
do **not** automatically imply an improvement over C, particularly on the
O3 scalar-SSA path.

Local differential tests extend the native bounds codegen fixture and
exercise in-range and index-equals-length dynamic slices at O0 and O3.
The comparison fixtures now cover stable sorting, reshuffled sorting,
16x16 integer matrix multiplication, and a **new** dependent unsigned
arithmetic checksum with the same formulas across Noqeri, C and Go.
The original arithmetic figures above may have used a different algorithm:
do not treat the new checksum's times as a historical before/after score.

On a full Linux x86-64 checkout, with no GitHub Actions:

```sh
cmake -S . -B build-local-perf -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-local-perf --target noqeri noqeri-native-bounds-fastpath-test --parallel 2
ctest --test-dir build-local-perf -R '^noqeri-native-bounds-fastpath$' --output-on-failure
python3 tests/native_bounds_local.py --compiler=build-local-perf/noqeri
python3 scripts/local-sorting-matrix-ab.py --candidate build-local-perf/noqeri
python3 scripts/local-native-vs-c.py --compiler build-local-perf/noqeri
```

For a real compiler improvement result, run
`scripts/local-sorting-matrix-ab.py --baseline /path/to/older/noqeri`
with both compilers built as Release on the **same** host. Inspect raw
medians, p95, output parity and native provenance. Until those runs
are available, the compiler build, native execution, and superiority to C
remain **unverified**, not passed.
