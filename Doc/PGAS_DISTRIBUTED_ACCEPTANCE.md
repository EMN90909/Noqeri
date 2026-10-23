# PGAS distributed expansion: acceptance and limitations

This feature set remains **experimental** on Noqeri 0.1.3. It adds useful
capabilities, but it is **not** evidence of complete, production-qualified PGAS.

## Compiler `@partition` (Stage-0)

A strict, compiler-recognized declaration:

```noqeri
@partition(strategy: "block", nodes: 4)
let dataset: [i64; 1024]
```

The Stage-0 lexer/parser/typechecker validates the strategy (block/cyclic), a
node count from 1 to 1024, and an uninitialized fixed i64 array of 1..1,000,000
elements. NIR retains the symbol and resolved distribution metadata, visible
through `noqeri nir file.noqeri`. This is **not distributed-address lowering**:
the interpreter, native backend and NQRI backend **explicitly reject execution**
rather than dangerously converting a remote array to local stack memory.
Stage-2/self-host compiler parity and native distributed-array indexing remain
unimplemented. Existing library APIs can route explicit global-index i64
operations through authenticated node clients.

## Safe remotely supplied programs

The `nq_pgas_execute_program` and `nq_pgas_tls_execute_program` APIs execute
an uploaded bytecode program, capped at 256 bytes and 4096 instructions. A
client's upload is isolated to that authenticated socket; the evaluator has a
32-item i64 stack and no filesystem, network, memory pointer, or FFI opcodes.
Supported opcodes: `0` noop, `1` push argument, `2` signed-i8 immediate,
`3` add, `4` subtract, `5` multiply, `6` divide, `7` duplicate,
`8` swap, `9` drop, `10` signed relative branch, `11` branch-if-zero,
`12` equality, `13` less-than, `14` return, `15` push zero.
Arithmetic overflow wraps for add/sub/mul; division by zero traps. A stalled
or endless loop exhausts fuel. Programs are **not arbitrary Noqeri closures or
native code**; they cannot call host services. Remote task execution remains
subject to timeout/uncertain-outcome rules.

## Optional real RDMA

`noqeri_pgas_rdma` is a Linux-only optional `libibverbs` backend with actual
RC QP setup, registered memory and one-sided i64 read/write work requests.
Peer descriptors include RDMA memory keys and MUST be exchanged through a
separate authenticated mTLS control channel. The provider **does not implement
that exchange for you**, nor does it encrypt the data path or persist direct
RDMA writes. Only enable RDMA on an appropriately secured private fabric.
If no usable device exists, the native ABI fails closed with `-ENODEV`;
without rdma-core headers/library it returns `-ENOTSUP`, not pretend-RDMA
over TCP. Test runners probe provider compilation/absence; **real RDMA NIC
pairing, completions, throughput and cross-host security remain unverified**.

## Synchronous two-node mirroring and epochs

The `noqeri_pgas_replication` library mirrors ordinary i64 store operations to
a TLS-connected standby and then the primary, reporting success only if both
respond successfully. The backing nodes MUST use durable WAL. It compares
both copies on verification reads and permanently stops new operations when
any result is ambiguous. It does not replicate CAS or fetch-add and it cannot
prevent writes through connections outside the mirror. Thus it is a strict
**application coordinator, not a distributed consensus engine**.

Durable nodes support a monotonically increasing `epoch` stored in the WAL.
After an operator externally verifies the previous primary is fenced or fully
terminated, they can promote the standby to a newer epoch. Stale epoch clients
are refused with `-ESTALE`. The test suite demonstrates controlled promotion,
independent WAL replay and stale-client rejection. **Node partitions and
dual-primary split brain are not solved**. No Raft/consensus, leases, global
barriers, off-host backups, snapshots or durable idempotency tokens are
provided. Automatic failover is **not safe** before those guarantees exist.

## Physical two-machine mTLS qualification

Build the operator-run executable:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target noqeri-pgas-cross-host-smoke
```

Provision your own CA with server SAN `pgas-node.example.internal` and an
allowed client SAN `pgas-client.example.internal`. Install the CA and
respective private credentials separately and securely on the two hosts.
Restrict the firewall to the TLS port; NEVER expose the legacy plaintext
node. Use disposable WAL paths and a unique 16..64 character loopback token.

**Host A (server):**

```sh
./build/noqeri-pgas-cross-host-smoke server 10.42.0.10 45010 \
  /var/lib/noqeri/pgas-smoke.wal '<unique-token>' ca.pem server.crt server.key \
  pgas-client.example.internal
```

**Host B (client):**

```sh
./build/noqeri-pgas-cross-host-smoke client 10.42.0.10 45010 \
  ca.pem client.crt client.key pgas-node.example.internal
```

The client must report `PGAS_CROSS_HOST_MTLS_PING_READ_WRITE_PASS`. Exercise
wrong SAN, revoked/replaced cert, disconnected server and a stateful restart.
Run on **two actual machines** and save run timestamps, software versions,
system logs, unique host identities and WAN/private-network characteristics
before claiming physical cross-host qualification. A GitHub-hosted single
runner cannot produce that evidence.

## Outstanding release gates

- Verified self-host Stage-2 parser/typechecker/NIR and distributed-array
  machine-code lowering through an explicit topology-aware runtime.
- Actual libibverbs RDMA pairs with authenticated peer-exchange protocol and
  security/packet-loss testing, plus intentional TLS fallback.
- A securely sandboxed general task VM with stronger language surface,
  per-tenant quotas and durable job deduplication/replay.
- Proven multi-node membership, consensus, leases, fencing, snapshots,
  checkpoints and automatic failover with idempotent client retries.
- Independent machine cross-host mTLS/RDMA interoperability, failure-injection,
  load, ASan/TSan/fuzz testing and verified Noqeri-linked native executables.

See issue #36 for the outstanding production gates.
