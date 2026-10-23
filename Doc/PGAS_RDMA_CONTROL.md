# RDMA authenticated peer exchange (experimental)

Noqeri 0.1.43 adds `noqeri_pgas_rdma_control`, a **TLS 1.3 mutual-certificate
control plane** for exchanging hardware RC queue pair (QP) descriptors before
calling `nq_pgas_rdma_connect`.

The control API is in `Include/noqeri/pgas_rdma_control.h` and the native
provider is `Runtime/native/pgas_rdma_control.cpp`. Unlike the optional
`libibverbs` data plane, the descriptor control target requires only
OpenSSL and POSIX sockets. Peers use:

1. Each node independently creates a real RDMA endpoint with
   `nq_pgas_rdma_open` and `nq_pgas_rdma_endpoint_local`.
2. Server starts an mTLS-only listener with an allowlisted client DNS SAN,
   trusted CA and server certificate/key. The client verifies the expected
   server DNS SAN and uses its client certificate/key.
3. Client and server exchange canonical **52-byte** protocol-versioned
   endpoint descriptors: QP number, PSN, rkey, registered address/slot bounds,
   local identifier (LID) and GID. Unknown versions, malformed or absent
   endpoint capabilities are rejected. A peer certificate signed by the CA
   without the expected SAN is rejected too.
4. Each process separately calls `nq_pgas_rdma_connect(local, &peer)`.
   RDMA errors, device absence and completion timeouts remain fail-closed.
   Do not auto-retry an uncertain write.

The control plane encrypts and authenticates descriptor exchange.
**It does NOT encrypt the subsequent one-sided RDMA data path.** Use a
private protected RDMA fabric with network ACLs, and do not assume
memory registration keys provide cryptographic access control.

The control-plane CTest exercises two independent TLS peers and deliberately
rejects an unauthorized CA-signed client. These tests use **synthetic endpoint
values** and prove the TLS serialization, not actual hardware QP pairing.
The existing optional libibverbs provider is compiled/probed independently.
Two real RDMA-capable hosts, authenticated descriptor exchange on those hosts,
successful QP transitions, read/write work completions, packet loss and NIC
performance measurements are still required for physical RDMA qualification.

This control API does not provide disk persistence for RDMA writes, cluster
membership, replicated epochs, automatic failover, credential rotation or
a general Noqeri FFI mapping for the C struct without ABI verification.
