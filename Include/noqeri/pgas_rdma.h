#ifndef NOQERI_PGAS_RDMA_H
#define NOQERI_PGAS_RDMA_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Experimental Linux libibverbs RC transport, remote i64 read/write only.
 * No remote atomics, no persistence, no automatic failover. A peer descriptor
 * includes a capability-bearing rkey and MUST ONLY be exchanged via a
 * separate, authenticated+encrypted mTLS control channel.
 * The caller owns and must close each node exactly once.
 */
typedef struct nq_pgas_rdma_node nq_pgas_rdma_node;
typedef struct nq_pgas_rdma_endpoint {
    uint32_t qp_number;
    uint32_t psn;
    uint32_t rkey;
    uint32_t slots;
    uint64_t address;
    uint16_t lid;
    uint8_t gid[16];
} nq_pgas_rdma_endpoint;
/* 1 = active verbs device exists, 0 = no active hardware, negative error
 * when this build has no provider (normally -ENOTSUP). */
int nq_pgas_rdma_available(void);
/* slots must be 1..1,000,000; port is active RDMA device port, gid index
 * selects local RoCE/IP-over-IB GID. On failure *out remains NULL. */
int nq_pgas_rdma_open(uint32_t slots, uint8_t port, uint8_t gid_index,
                       nq_pgas_rdma_node **out);
int nq_pgas_rdma_endpoint_local(nq_pgas_rdma_node *node,
                                  nq_pgas_rdma_endpoint *out);
/* The caller MUST verify peer authorization and protect descriptor exchange.
 * The provider checks QP metadata, bounds and completion status, but does not
 * authenticate remote rkeys, encrypt the RDMA data path or provide a lease. */
int nq_pgas_rdma_connect(nq_pgas_rdma_node *node,
                          const nq_pgas_rdma_endpoint *remote);
int nq_pgas_rdma_read(nq_pgas_rdma_node *node, uint32_t remote_slot,
                       uint32_t timeout_ms, int64_t *value);
int nq_pgas_rdma_write(nq_pgas_rdma_node *node, uint32_t remote_slot,
                        int64_t value, uint32_t timeout_ms);
int nq_pgas_rdma_close(nq_pgas_rdma_node *node);
#ifdef __cplusplus
}
#endif
#endif
