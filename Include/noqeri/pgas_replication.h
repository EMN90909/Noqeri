#ifndef NOQERI_PGAS_REPLICATION_H
#define NOQERI_PGAS_REPLICATION_H
#include <stdint.h>
#include "pgas_tls_gateway.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Strict two-node synchronous mirror. Caller owns both TLS connections.
 * A write is ACKed only after standby AND primary separately fsync their WAL.
 * IMPORTANT: this is NOT Raft, automatic leader election, or split-brain-safe
 * consensus. ALL application writes must flow through this coordinator and
 * both underlying nodes must have durable WALs. If a write becomes ambiguous,
 * the pair becomes permanently fenced until an operator reconciles it.
 * Recovery/promotion REQUIRES externally verified fencing of the old leader.
 */
typedef struct nq_pgas_replica_pair nq_pgas_replica_pair;
int nq_pgas_replica_pair_create(nq_pgas_tls_client *primary,
    nq_pgas_tls_client *standby, nq_pgas_replica_pair **out);
int nq_pgas_replica_pair_write(nq_pgas_replica_pair *pair,
    uint32_t slot, int64_t value);
int nq_pgas_replica_pair_read_verified(nq_pgas_replica_pair *pair,
    uint32_t slot, int64_t *value);
int nq_pgas_replica_pair_close(nq_pgas_replica_pair *pair);
#ifdef __cplusplus
}
#endif
#endif
