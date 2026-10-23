# PGAS phase 2 qualification status

This document describes what the experimental PGAS components **actually**
support, not a production-ready distributed-memory platform.

## Version and bootstrap provenance

The active Noqeri project and C++ toolchain report 0.1.3. The immutable,
checksum-verified Stage-1 seed and its self-hosted source still report 0.7.0.
Rebuilding Stage-1 from modified self-hosted source requires reseeding and
independent multi-stage reproducibility evidence, not a cosmetic string edit.
This does not qualify 0.1.3 as a stable compiler release.

## Native features and trust boundaries

- Durable i64 node state: `nq_pgas_server_start_durable` opens a mode-0600
  exclusive POSIX WAL at an absolute path. Acknowledged mutations are fsynced
  before replying. On restart, complete entries replay; truncated tails are
  discarded; corrupt complete entries fail closed. The WAL is capped at 64 MiB.
  There are no snapshots, off-host replicas, failover or bounded retry IDs.
  Crash recovery is local to one node and does not guarantee cluster recovery.
- Remote transport: `nq_pgas_tls_gateway` accepts TLS **1.3 only** connections,
  requiring a CA-validated client certificate and allowlisted client SAN DNS.
  The client verifies its expected server DNS and validates the server
  certificate. The gateway connects to a loopback PGAS node with the local
  shared token. Never expose the underlying loopback RPC to an untrusted host.
  Certificates are configured at startup; rotate/restart to update trust.
- Distributed tasks: node owners can register a trusted callback under a
  numeric ID (`nq_pgas_register_task`). Client nodes can call the registered
  kernel over TLS with one signed-64-bit argument and result. It is NOT remote
  executable code upload, a sandbox, or a distributed closure scheduler.
  Callback execution is concurrent and not automatically replay-safe.
- The bootstrap compiler now recognizes and validates the declared
  `@partition(strategy: "block"|"cyclic", nodes: N)` syntax on explicitly
  typed fixed i64 arrays. Crucially, it emits a specific diagnostic and
  **fails compilation** rather than silently lowering it to local storage.
  Allocation, typed remote references, native code generation and Stage-2
  compiler parity for this attribute remain unimplemented. Global address
  mapping is still a library/ABI operation.
- Memory ordering is node-local. Global barriers, global linearizability,
  distributed transactions, leases, fencing and membership are absent.
- RDMA is **not** implemented, and there is no hardware-tested verbs/UCX
  provider; the portable fallback is TCP/TLS only.
- A two-container test on independent network namespaces covers mTLS
  encrypted RPC, remote atomics and registered worker execution. It is
  **not** a two-physical-machine certification.
- Full Noqeri-native machine-code linking, 2+ physical-machine integration,
  failure-injection matrices, certificate rotation, node revocation,
  high-throughput load tests, and hardened production deployment are
  outstanding.

## Demonstrable tests

The `pgas-native.yml` GitHub Actions workflow exercises:
partition placement, loopback RPC, atomic operations, registered tasks,
cross-process networking, crash detection, WAL recovery after SIGKILL,
incomplete WAL record handling, corrupt-record rejection, TLS 1.3
mutual-certificate communication and wrong-hostname rejection.
CMake builds the runtime and Noqeri source-checks the FFI modules.
These are **single-runner tests**, not multi-machine qualification.

## Handling remote write failures

A timeout or disconnect after sending a mutation means its outcome is unknown.
Do **not** blindly retry non-idempotent `fetch_add` or remote task executions.
An authenticated transport does not prevent application-level duplicate work.
Introduce operation IDs, durable deduplication and epoch fencing before
implementing automatic retry or replica failover.
