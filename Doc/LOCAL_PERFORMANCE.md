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
