# PGAS in Noqeri: experimental runtime (0.1.3 language track)

Phase 2 adds an experimental TLS 1.3 mutual-certificate gateway, local
durable WAL recovery, and allowlisted remote worker kernels. See
[qualification and trust boundaries](PGAS_PHASE2_STATUS.md) and [experimental distributed acceptance](PGAS_DISTRIBUTED_ACCEPTANCE.md) for the current
implementation matrix and tests.

This feature is **not a full fault-tolerant multi-host PGAS runtime**. The first implementation
provides a native POSIX loopback RPC server, per-node `i64` storage, a deterministic
block/cyclic global-index mapping, linearizable per-node read/write/CAS/fetch-add,
node-local barriers, and bounded connection timeouts. The native host is opt-in.

## Model

A global element index is resolved to `(owner node, local slot)` through
`nq_pgas_location` / `noqeri.std.pgas.pgas_locate`. Users configure one server
per node/process, allocate the expected number of slots, establish a separate
client connection to each server, then operate on the resolved `(node, slot)`.
No remote memory pointer is exposed. All i64 mutations on one node are ordered
under one mutex; the protocol has sequentially consistent per-node operation
ordering, **not** global multi-node ordering. Cross-node transactions and
cluster-wide barriers are not supported.

CMake builds `noqeri_pgas_runtime` (Linux/POSIX) and runs
`noqeri-pgas-runtime` via CTest when `BUILD_TESTING=ON`. The C ABI header is
`Include/noqeri/pgas_runtime.h`; Noqeri declarations are in `Lib/std/pgas.noqeri`.
The host ABI must be explicitly linked into the consuming native executable;
`noqeri check` alone does not link external host symbols.

The C API returns zero on success and negative errno-style errors on failure.
Native handles must be closed exactly once. No operation is automatically retried.
If a connection breaks during a write or atomic operation, execution may already
have happened: report an indeterminate outcome to the application rather than
blindly replaying non-idempotent operations. Barrier timeouts abort the current
node-local generation; participants must coordinate before attempting another.

## Scope and security

- TCP bindings are **restricted to 127.0.0.1** and non-loopback client endpoints
  are rejected. A shared secret (16 to 64 printable non-whitespace bytes) gates
  access. The secret is sent unencrypted and is **only acceptable for local
  development**. Never expose the server through a public proxy/tunnel.
- The protocol has fixed request and response sizes, bounded allocations, a
  64-connection lifetime cap, five-second barrier timeouts, and a maximum of
  one million i64 slots per server.
- The server uses concurrent POSIX threads for independent clients. This is
  **not** a general-purpose remote task scheduler and does not ship executable
  code or lambda closures across nodes.
- The legacy loopback-only protocol remains plaintext. Use the **separate**
  `noqeri_pgas_tls_gateway` for TLS 1.3/mTLS; direct remote access to the
  plaintext node is intentionally forbidden.
- Experimental durable local WAL, registered task dispatch and mTLS are
  available, but RDMA, cluster membership/leader election, replicated
  durable data, global memory consistency, failover, idempotency keys, and
  cluster-wide barriers are **not implemented**. Production deployment is
  not qualified.
- Partition placement is an explicit library/ABI operation. Stage-0
  `@partition(...)` syntax is compiler-recognized and, as of 0.1.43,
  native x86-64 direct i64 indexing lowers through explicit remote calls.
  It requires a bound topology and is not yet available in Stage-2 or NQRI.
  See [native arrays](PGAS_NATIVE_ARRAYS.md).

## Next qualification gates

1. Establish a TLS/mTLS-capable cross-host transport with certificate rotation,
   membership authorization, rate limiting, and network fault injection.
2. Implement operation IDs with bounded durable deduplication, lease/epoch
   fencing, state snapshots, recovery, and read/write consistency guarantees.
3. Add distributed task queues, failure-aware cluster barriers, optional RDMA,
   structured partition metadata and verified compiler lowering.
4. Test native Noqeri linking, sanitizer builds, process-kill recovery,
   multi-machine latency, load, and mixed-version interoperability.
