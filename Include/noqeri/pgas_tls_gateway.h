#ifndef NOQERI_PGAS_TLS_GATEWAY_H
#define NOQERI_PGAS_TLS_GATEWAY_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Experimental mTLS 1.3 gateway for the fixed-size PGAS RPC protocol.
 * Server certificate, client certificate, CA file and peer DNS identities
 * are mandatory. Connection is authenticated at both ends.
 * This is not a general RPC service or a native RDMA provider.
 *
 * Certificates should have the corresponding SAN DNS names.
 * Binds only the requested numeric IPv4 interface. All errors are negative
 * errno-style integers. Close each opaque handle exactly once.
 */
typedef struct nq_pgas_tls_gateway nq_pgas_tls_gateway;
typedef struct nq_pgas_tls_client nq_pgas_tls_client;
int nq_pgas_tls_gateway_start(const char *bind_ipv4, uint16_t port,
    uint16_t local_pgas_port, const char *backend_token,
    const char *ca_pem, const char *server_cert_pem, const char *server_key_pem,
    const char *allowed_client_dns, nq_pgas_tls_gateway **out);
uint16_t nq_pgas_tls_gateway_port(nq_pgas_tls_gateway *gateway);
int nq_pgas_tls_gateway_stop(nq_pgas_tls_gateway *gateway);
int nq_pgas_tls_client_connect(const char *server_ipv4, uint16_t port,
    const char *ca_pem, const char *client_cert_pem, const char *client_key_pem,
    const char *expected_server_dns, uint32_t timeout_ms, nq_pgas_tls_client **out);
int nq_pgas_tls_client_close(nq_pgas_tls_client *client);
int nq_pgas_tls_ping(nq_pgas_tls_client *client);
int nq_pgas_tls_read(nq_pgas_tls_client *client, uint32_t index, int64_t *value);
int nq_pgas_tls_write(nq_pgas_tls_client *client, uint32_t index, int64_t value);
int nq_pgas_tls_compare_exchange(nq_pgas_tls_client *client, uint32_t index,
    int64_t expected, int64_t desired, int64_t *observed);
int nq_pgas_tls_fetch_add(nq_pgas_tls_client *client, uint32_t index,
    int64_t delta, int64_t *previous);
int nq_pgas_tls_barrier(nq_pgas_tls_client *client);
#ifdef __cplusplus
}
#endif
#endif
