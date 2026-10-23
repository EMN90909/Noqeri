#ifndef NOQERI_PGAS_RDMA_CONTROL_H
#define NOQERI_PGAS_RDMA_CONTROL_H
#include <stdint.h>
#include "pgas_rdma.h"
#ifdef __cplusplus
extern "C" {
#endif
/* TLS 1.3 + mutual certificate authentication for libibverbs QP descriptor
 * exchange. The data path is not TLS-encrypted and requires a trusted RDMA
 * fabric. This control protocol does not create, allocate, or attest a NIC.
 * Session descriptor exchange fails closed on missing/invalid identities.
 */
typedef struct nq_pgas_rdma_control_server nq_pgas_rdma_control_server;
int nq_pgas_rdma_control_server_start(const char *bind_ipv4,uint16_t port,
    const char *ca_pem,const char *server_cert_pem,const char *server_key_pem,
    const char *allowed_client_dns,nq_pgas_rdma_control_server **out);
uint16_t nq_pgas_rdma_control_server_port(nq_pgas_rdma_control_server *);
int nq_pgas_rdma_control_server_exchange(nq_pgas_rdma_control_server *,
    const nq_pgas_rdma_endpoint *local,nq_pgas_rdma_endpoint *remote,
    uint32_t timeout_ms);
int nq_pgas_rdma_control_server_stop(nq_pgas_rdma_control_server *);
int nq_pgas_rdma_control_client_exchange(const char *server_ipv4,uint16_t port,
    const char *ca_pem,const char *client_cert_pem,const char *client_key_pem,
    const char *expected_server_dns,const nq_pgas_rdma_endpoint *local,
    nq_pgas_rdma_endpoint *remote,uint32_t timeout_ms);
#ifdef __cplusplus
}
#endif
#endif
