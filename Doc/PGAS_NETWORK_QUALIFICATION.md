# PGAS multi-host qualification

The `pgas-network.yml` CI workflow creates two separate containers with
independent processes and network namespaces, and verifies that a node
accessible only through TLS 1.3/mTLS responds to remote read, write,
fetch-add, and a registered native task through the container network.

**This does not constitute two-physical-machine validation.** The two
containers share a Linux host kernel, and the ephemeral CA and certificates
are test credentials only. The CI environment provides no RDMA hardware,
out-of-band node identity service, or real multi-host failure recovery.

To qualify actual cross-machine deployment:
1. Provision two independently administered hosts and an isolated test network.
2. Issue server and client certificates from a private CA with DNS SANs;
   maintain a deny-by-default node identity allowlist and revocation process.
3. Start the PGAS backend bound only to loopback on server host A; start
   `nq_pgas_tls_gateway_start` on its routable IPv4 address.
4. Use the TLS client on host B with CA validation and expected DNS identity.
   Include unauthorized, expired, rotated and revoked certificates; simulate
   TCP resets, disconnects, host restarts and NAT/firewall conditions.
5. Verify acknowledged WAL writes survive SIGKILL/power interruption on
   host A and are recovered only by its exclusive owner. Do not assert failover
   to host B without a separately implemented epoch-fenced replicated WAL.
6. Measure latency and throughput, and validate hardware RDMA only with an
   actual RDMA-capable device and isolated protection domain.

Security caution: the plaintext backend shared token never traverses the
cross-host link, but the gateway currently proxies to the loopback token
backend and supports only static client SAN authorization configured at start.
