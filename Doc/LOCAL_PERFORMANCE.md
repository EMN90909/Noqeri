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
