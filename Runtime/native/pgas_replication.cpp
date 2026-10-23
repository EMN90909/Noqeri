#include "noqeri/pgas_replication.h"
#include <cerrno>
#include <mutex>
#include <new>

struct nq_pgas_replica_pair {
  nq_pgas_tls_client *primary = nullptr, *standby = nullptr;
  std::mutex mu;
  bool uncertain = false;
};
extern "C" {
int nq_pgas_replica_pair_create(nq_pgas_tls_client *primary,
    nq_pgas_tls_client *standby,nq_pgas_replica_pair **out) {
  if (!out || !primary || !standby || primary == standby) return -EINVAL;
  *out = nullptr;
  if (nq_pgas_tls_ping(primary) != 0 || nq_pgas_tls_ping(standby) != 0)
    return -ENOTCONN;
  auto *pair = new(std::nothrow) nq_pgas_replica_pair;
  if(!pair) return -ENOMEM;
  pair->primary=primary;
  pair->standby=standby;
  *out=pair;
  return 0;
}
int nq_pgas_replica_pair_write(nq_pgas_replica_pair *pair,
    uint32_t slot,int64_t value) {
  if (!pair) return -EINVAL;
  std::lock_guard<std::mutex> lock(pair->mu);
  if(pair->uncertain) return -EUCLEAN;
  // Commit to standby first. A successful caller acknowledgement means
  // both replicas have confirmed fsynced commits (durable node contract).
  // On ANY failure, refuse new writes until explicit external reconciliation.
  const int secondary=nq_pgas_tls_write(pair->standby,slot,value);
  if (secondary != 0) {pair->uncertain=true;return -EUCLEAN;}
  const int primary=nq_pgas_tls_write(pair->primary,slot,value);
  if (primary != 0) {pair->uncertain=true;return -EUCLEAN;}
  return 0;
}
int nq_pgas_replica_pair_read_verified(nq_pgas_replica_pair *pair,
    uint32_t slot,int64_t *value) {
  if(!pair||!value) return -EINVAL;
  std::lock_guard<std::mutex> lock(pair->mu);
  if(pair->uncertain)return -EUCLEAN;
  int64_t leader=0,other=0;
  if(nq_pgas_tls_read(pair->primary,slot,&leader) != 0 ||
     nq_pgas_tls_read(pair->standby,slot,&other) != 0){
    pair->uncertain=true;return -EHOSTUNREACH;
  }
  if(leader!=other) {pair->uncertain=true;return -EUCLEAN;}
  *value=leader;
  return 0;
}
int nq_pgas_replica_pair_close(nq_pgas_replica_pair *pair) {
  if(!pair)return -EINVAL;
  delete pair;
  return 0;
}
}
