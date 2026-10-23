# PGAS qualification status (phase 2 and experimental distributed extensions)

**Current implementation matrix and operator-run acceptance steps:**
[Distributed PGAS acceptance](PGAS_DISTRIBUTED_ACCEPTANCE.md).

This document describes what the experimental PGAS components **actually**
support, not a production-ready distributed-memory platform.

## Version and bootstrap provenance

The active Noqeri project and C++ toolchain report 0.1.43. The immutable,
checksum-verified Stage-1 seed and its self-hosted source still report 0.7.0.
Rebuilding Stage-1 from modified self-hosted source requires reseeding and
independent multi-stage reproducibility evidence, not a cosmetic string edit.
This does not qualify 0.1.43 as a stable compiler release.

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
  numeric ID (`nq_pgas_register_task`) with one signed-64-bit argument and
  result. The additional bounded sandbox bytecode runtime permits unprivileged
  client-supplied arithmetic programs with strict fuel, stack and size caps.
  Stage-0 also compiles a small pure Noqeri `remote(i64): i64` function
  with baked immutable captures into this sandbox VM. That profile has
  end-to-end execution tests but cannot serialize general closures or
  perform effects. See [remote programs](PGAS_REMOTE_PROGRAMS.md).
  Callback execution is concurrent and not automatically replay-safe.
- Stage-0 recognizes, typechecks and retains `@partition` metadata.
  In 0.1.43, native x86-64 AOT emits real remote indexed `i64` reads/writes
  through `nq_pgas_partition_bind` or the mTLS-backed bind API. The native
  compiler no longer silently falls back to local memory; remote errors
  abort. Independent AOT and TLS tests demonstrate storage placement.
  Interpreter and NQRI continue to fail closed. Stage-2 parity, general
  distributed types and native RDMA selection remain missing.
  See [native arrays](PGAS_NATIVE_ARRAYS.md).
- Node epochs are now WAL-durable and stale client operations are refused
  after controlled external fencing/promotion. A two-node synchronous TLS
  mirror exists, but split-brain-safe leader election, global barriers,
  global linearizability, distributed transactions and consensus membership
  are absent.
- Optional Linux `libibverbs` RC RDMA endpoint registration, QP setup and
  one-sided read/write implementation is present. CI compiles the real provider
  and probes hardware absence, but **two-NIC peer qualification, authenticated
  descriptor exchange, persistence and packet-level security are not yet
  integrated**; TLS remains the qualified test path.
- Native Stage-0 Noqeri-generated machine-code linking for fixed i64
  partition arrays has passed local and TLS-gateway tests. Stage-2 compiler
  parity, 2+ physical-machine integration,
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
