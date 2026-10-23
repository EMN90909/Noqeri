#ifndef NOQERI_PGAS_RUNTIME_H
#define NOQERI_PGAS_RUNTIME_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Experimental POSIX, loopback-only PGAS transport. Errors are negative
 * errno numbers; successful operations return zero. No TLS or RDMA.
 * Opaque handles are owned by the caller and must be closed exactly once.
 */
typedef struct nq_pgas_server nq_pgas_server;
typedef struct nq_pgas_client nq_pgas_client;
/* Explicitly registered worker kernels, not arbitrary uploaded machine code.
 * The callback and context must remain alive until server shutdown; callback
 * invocation is concurrent and must protect its own state. Only 1..65535 IDs
 * are accepted; operation results are not transparently retried. */
typedef int (*nq_pgas_task_callback)(int64_t input, void *context, int64_t *output);
int nq_pgas_register_task(nq_pgas_server *server, uint32_t task_id,
                          nq_pgas_task_callback callback, void *context);
int nq_pgas_run_task(nq_pgas_client *client, uint32_t task_id,
                     int64_t argument, int64_t *output);
/* Execute client-supplied, sandboxed arithmetic bytecode (not host machine
 * code). A private per-connection upload, at most 256 bytes; fuel 1..4096.
 * No memory/file/network access or native calls. Failure is negative errno.
 * Non-idempotent remote execution must NOT be blindly replayed. */
int nq_pgas_execute_program(nq_pgas_client *client, const uint8_t *bytecode,
                            size_t length, int64_t argument, uint32_t fuel,
                            int64_t *output);
/* Tokens must be 16..64 ASCII bytes, with no whitespace or control chars.
 * A port of zero requests an ephemeral port. slots must be nonzero.
 * barrier_participants=0 disables the barrier on this node. */
int nq_pgas_server_start(uint16_t port, const char *token, uint32_t slots,
                         uint32_t barrier_participants, nq_pgas_server **out);
/* Absolute path to a mode-0600 WAL. Exclusive lock prevents simultaneous
 * owners. Every acknowledged mutation is fsynced before reply. On restart,
 * complete valid records replay; truncated tails are discarded. Complete
 * malformed records fail closed (-EBADMSG). No replicas or transparent retries.
 * Bounded log: once full, mutations return -ENOSPC until offline maintenance. */
int nq_pgas_server_start_durable(uint16_t port, const char *token, uint32_t slots,
                                 uint32_t barrier_participants,
                                 const char *wal_path, nq_pgas_server **out);
uint16_t nq_pgas_server_port(nq_pgas_server *server);
int nq_pgas_server_stop(nq_pgas_server *server);
/* Rejects non-loopback hosts, intentionally. */
int nq_pgas_client_connect(const char *host, uint16_t port, const char *token,
                           uint32_t timeout_ms, nq_pgas_client **out);
int nq_pgas_client_close(nq_pgas_client *client);
int nq_pgas_ping(nq_pgas_client *client);
int nq_pgas_read(nq_pgas_client *client, uint32_t local_index, int64_t *out);
int nq_pgas_write(nq_pgas_client *client, uint32_t local_index, int64_t value);
int nq_pgas_compare_exchange(nq_pgas_client *client, uint32_t local_index,
                             int64_t expected, int64_t desired, int64_t *observed);
int nq_pgas_fetch_add(nq_pgas_client *client, uint32_t local_index,
                      int64_t delta, int64_t *previous);
/* Barrier requires exactly barrier_participants simultaneous calls; timeout
 * breaks the current generation for ALL waiters (no infinite hang). */
int nq_pgas_barrier(nq_pgas_client *client);
/* Deterministic placement. strategy: 0=block, 1=cyclic.
 * Returned local_index is an index within the selected server's slots. */
int nq_pgas_location(uint64_t index, uint64_t elements, uint32_t nodes,
                     uint32_t strategy, uint32_t *owner, uint32_t *local_index);
#ifdef __cplusplus
}
#endif
#endif
