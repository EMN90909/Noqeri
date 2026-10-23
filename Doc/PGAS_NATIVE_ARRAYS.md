# Noqeri 0.1.43: native-compiled PGAS i64 arrays (experimental)

The Stage-0 compiler can now lower a restricted partitioned declaration into
**real remote read/write calls** in native x86-64 assembly:

```noqeri
@partition(strategy: "cyclic", nodes: 2)
let readings: [i64; 6]

readings[0] = 101 as i64
readings[1] = 202 as i64
readings[2] = readings[0] + 5 as i64
print(readings[2])
```

The compile command writes assembly, not a complete executable:

```sh
./build/noqeri build tests/pgas_partition_native.noqeri build/readings.s -O0
```

Link generated assembly with `Runtime/native/pgas_runtime.cpp`,
`Runtime/native/pgas_tls_gateway.cpp` and `Runtime/native/pgas_partition.cpp`
(or the corresponding static libraries), plus OpenSSL, libcrypto and pthreads.
A native application **must** provision and authenticate node connections and
call `nq_pgas_partition_bind("readings", 6, 2, 1, clients)` or the encrypted
`nq_pgas_partition_bind_tls(...)` *before* entering `noqeri_entry()`.
For strategy 0 = block, 1 = cyclic. Connections are caller-owned; unbind before
closing them. TLS clients must be authenticated through the pinned server DNS
and private CA before binding. Do not expose the plain loopback backend.

The compiler emits `nq_pgas_compiler_partition_read` and
`nq_pgas_compiler_partition_write`, with the array name, index and topology
baked in. Ownership is resolved at runtime. Topology mismatch, invalid
indices, unbound clients, network failures and stale epochs **abort** instead
of silently returning local or stale data. This fail-stop behavior must be
handled by a supervisor for long-running services. A remote mutation might
have committed when a disconnect is observed; do not blindly retry it.

**Supported scope:** module-entry fixed-size `[i64; N]` arrays with direct
index read/write and constant-time `len(array)`. Declaration names, node
counts, strategy and element count are static. No arrays of records, pointer
escape, slices, address-of elements, parameter passing, functions accessing
the partition, dynamic topology changes, or distributed transactions.
Parallel multithreaded access to the shared binding is serialized; node-level
atomicity applies per operation, **not globally across nodes**.

**Not yet supported:** full self-hosted Stage-2/NQRI compiler parity,
interpreter execution of partition arrays, native compiler transport
negotiation with RDMA, type-general distributed references, hardware RDMA
qualification, automatic cluster failover and consensus membership.
The interpreter and NQRI serializer keep rejecting partitioned programs.

## End-to-end evidence

The PGAS native workflow builds the Noqeri compiler, checks AST/NIR semantics,
compiles the example to x86-64 assembly, links a real C++/Noqeri executable,
starts two independent PGAS node processes, and verifies block/cyclic owner
placement through the native ABI. It additionally runs the compiled code over
two independent mutually authenticated TLS 1.3 gateways and verifies
failure on missing binding. See
`tests/scripts/pgas-native-partition-link.sh`.

The TLS gateway test uses independent connections on the same runner.
Physical multi-machine tests and actual paired NIC RDMA operations require
separately provisioned machines and retained test evidence. Do not equate
simulated or loopback tests with that qualification.
