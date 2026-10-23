# Restricted Noqeri remote closures in v0.1.43

The Stage-0 compiler now has a `pgas-compile` target for a **pure,
bounded subset of Noqeri**. It produces portable `.nqpg` sandbox bytecode
for the existing PGAS arithmetic VM, not a native program sent across the
network.

Example:

```noqeri
const bias: i64 = 5 as i64
export function remote(value: i64): i64 {
    return value + bias
}
```

Compile:

```sh
./build/noqeri pgas-compile tests/pgas_remote_closure.noqeri build/task.nqpg
```

An authenticated caller can submit the resulting bytes using
`nq_pgas_execute_program` or `nq_pgas_tls_execute_program`. The remote
sandbox runs with a strict maximum of 256 bytecode bytes, 32 i64 stack slots
and a 1..4096 instruction fuel counter.

Supported: exactly one exported `remote(i64): i64` function, a single pure
return expression composed of the parameter, i8-range integer literals,
module-scope immutable i64 literal captures, and `+`, `-`, `*`, `/`.
Captures are baked into the binary at compile time, not dynamically read
from the caller's memory. The function cannot call another function,
allocate, access arrays/pointers, use the filesystem, network, FFI, loops,
or execute native code. Unsafe programs are rejected at compilation.
Division by zero and fuel exhaustion fail with a sandbox error.

The end-to-end CI test compiles a captured Noqeri function to bytecode,
executes it through a real PGAS node and verifies both its result and error
behavior. Additional negative tests reject mutable captures, host calls,
multi-function programs and oversized immediates.

This is a **small compileable pure-closure profile**, not arbitrary
Noqeri closure serialization, remote multi-function apps, coroutine/task
scheduling or durable retry-safe jobs. A timeout after remote execution may
represent an already completed task. Proper replay-safe operation IDs,
persistent queueing, worker isolation, cancellation, and cluster fencing
must be implemented before automatically retrying effects.
