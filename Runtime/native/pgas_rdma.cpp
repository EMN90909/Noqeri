#include "noqeri/pgas_rdma.h"
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <limits>
#ifdef NOQERI_HAS_IBVERBS
#include <infiniband/verbs.h>
#include <sys/random.h>
#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

struct nq_pgas_rdma_node {
  ibv_context *ctx = nullptr;
  ibv_pd *pd = nullptr;
  ibv_cq *cq = nullptr;
  ibv_qp *qp = nullptr;
  ibv_mr *memory_mr = nullptr;
  ibv_mr *scratch_mr = nullptr;
  ibv_port_attr port_attr{};
  std::vector<int64_t> memory;
  alignas(8) uint64_t scratch = 0;
  nq_pgas_rdma_endpoint local{}, peer{};
  uint8_t port = 1, gid_index = 0;
  bool connected = false, unusable = false;
  std::mutex mu;
};
namespace {
static bool gid_has_bytes(const uint8_t *gid) {
  for (size_t i = 0; i < 16; ++i) if (gid[i]) return true;
  return false;
}
static void destroy(nq_pgas_rdma_node *n) {
  if (!n) return;
  if (n->qp) ibv_destroy_qp(n->qp);
  if (n->memory_mr) ibv_dereg_mr(n->memory_mr);
  if (n->scratch_mr) ibv_dereg_mr(n->scratch_mr);
  if (n->cq) ibv_destroy_cq(n->cq);
  if (n->pd) ibv_dealloc_pd(n->pd);
  if (n->ctx) ibv_close_device(n->ctx);
  delete n;
}
static int submit(nq_pgas_rdma_node *n, uint32_t slot, bool write,
                  uint32_t timeout_ms, int64_t *value) {
  if (!n || !value || !timeout_ms || timeout_ms > 60000) return -EINVAL;
  std::lock_guard<std::mutex> lock(n->mu);
  if (!n->connected || n->unusable) return -ENOTCONN;
  if (slot >= n->peer.slots) return -ERANGE;
  if (n->peer.address > UINT64_MAX - static_cast<uint64_t>(slot) * 8) return -EOVERFLOW;
  if (write) n->scratch = static_cast<uint64_t>(*value);
  ibv_sge sge{};
  sge.addr = reinterpret_cast<uintptr_t>(&n->scratch);
  sge.length = 8;
  sge.lkey = n->scratch_mr->lkey;
  ibv_send_wr wr{}, *bad = nullptr;
  wr.wr_id = 1;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.opcode = write ? IBV_WR_RDMA_WRITE : IBV_WR_RDMA_READ;
  wr.send_flags = IBV_SEND_SIGNALED;
  wr.wr.rdma.remote_addr = n->peer.address + static_cast<uint64_t>(slot) * 8;
  wr.wr.rdma.rkey = n->peer.rkey;
  const int rc = ibv_post_send(n->qp, &wr, &bad);
  if (rc != 0) { n->unusable = true; return -rc; }
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  for (;;) {
    ibv_wc wc{};
    int count = ibv_poll_cq(n->cq, 1, &wc);
    if (count < 0) { n->unusable = true; return -EIO; }
    if (count == 1) {
      if (wc.wr_id != 1 || wc.status != IBV_WC_SUCCESS) {
        n->unusable = true; return -EIO;
      }
      if (!write) *value = static_cast<int64_t>(n->scratch);
      return 0;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      // The operation's outcome is ambiguous: fail closed, no retry.
      n->unusable = true; return -ETIMEDOUT;
    }
    std::this_thread::yield();
  }
}
}

extern "C" {
int nq_pgas_rdma_available(void) {
  int count = 0;
  ibv_device **devices = ibv_get_device_list(&count);
  if (!devices) return -EIO;
  int available = 0;
  for (int i = 0; i < count && !available; ++i) {
    ibv_context *ctx = ibv_open_device(devices[i]);
    if (!ctx) continue;
    ibv_device_attr device{};
    if (ibv_query_device(ctx, &device) == 0) {
      for (unsigned port = 1; port <= device.phys_port_cnt; ++port) {
        ibv_port_attr attributes{};
        if (ibv_query_port(ctx, port, &attributes) == 0 &&
            attributes.state == IBV_PORT_ACTIVE) { available = 1; break; }
      }
    }
    ibv_close_device(ctx);
  }
  ibv_free_device_list(devices);
  return available;
}
int nq_pgas_rdma_open(uint32_t slots, uint8_t port, uint8_t gid_index,
                      nq_pgas_rdma_node **out) {
  if (!out || slots == 0 || slots > 1000000 || port == 0) return -EINVAL;
  *out = nullptr;
  int count = 0;
  ibv_device **devices = ibv_get_device_list(&count);
  if (!devices) return -EIO;
  std::unique_ptr<nq_pgas_rdma_node> n(new (std::nothrow) nq_pgas_rdma_node);
  if (!n) { ibv_free_device_list(devices); return -ENOMEM; }
  for (int i = 0; i < count; ++i) {
    ibv_context *candidate = ibv_open_device(devices[i]);
    if (!candidate) continue;
    ibv_device_attr dev{};
    ibv_port_attr attributes{};
    if (ibv_query_device(candidate, &dev) == 0 && port <= dev.phys_port_cnt &&
        ibv_query_port(candidate, port, &attributes) == 0 &&
        attributes.state == IBV_PORT_ACTIVE) {
      n->ctx = candidate;
      n->port_attr = attributes;
      break;
    }
    ibv_close_device(candidate);
  }
  ibv_free_device_list(devices);
  if (!n->ctx) return -ENODEV;
  n->port = port; n->gid_index = gid_index;
  ibv_gid gid{};
  if (ibv_query_gid(n->ctx, port, gid_index, &gid) != 0) {
    destroy(n.release()); return -EINVAL;
  }
  n->pd = ibv_alloc_pd(n->ctx);
  if (!n->pd) { destroy(n.release()); return -ENOMEM; }
  n->cq = ibv_create_cq(n->ctx, 16, nullptr, nullptr, 0);
  if (!n->cq) { destroy(n.release()); return -ENOMEM; }
  try { n->memory.resize(slots, 0); }
  catch (...) { destroy(n.release()); return -ENOMEM; }
  n->memory_mr = ibv_reg_mr(n->pd, n->memory.data(), slots * sizeof(int64_t),
        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
  if (!n->memory_mr) { int e = errno ? errno : ENOMEM; destroy(n.release()); return -e; }
  n->scratch_mr = ibv_reg_mr(n->pd, &n->scratch, sizeof(n->scratch), IBV_ACCESS_LOCAL_WRITE);
  if (!n->scratch_mr) { int e = errno ? errno : ENOMEM; destroy(n.release()); return -e; }
  ibv_qp_init_attr init{};
  init.send_cq = n->cq; init.recv_cq = n->cq;
  init.qp_type = IBV_QPT_RC;
  init.cap.max_send_wr = 8; init.cap.max_recv_wr = 1;
  init.cap.max_send_sge = 1; init.cap.max_recv_sge = 1;
  n->qp = ibv_create_qp(n->pd, &init);
  if (!n->qp) { int e = errno ? errno : EIO; destroy(n.release()); return -e; }
  uint32_t random_psn = 0;
  if (getrandom(&random_psn, sizeof(random_psn), 0) != sizeof(random_psn)) {
    destroy(n.release()); return -EIO;
  }
  n->local.qp_number = n->qp->qp_num;
  n->local.psn = random_psn & 0x00ffffff;
  n->local.rkey = n->memory_mr->rkey;
  n->local.slots = slots;
  n->local.address = reinterpret_cast<uint64_t>(n->memory.data());
  n->local.lid = n->port_attr.lid;
  std::memcpy(n->local.gid, gid.raw, sizeof(n->local.gid));
  *out = n.release();
  return 0;
}
int nq_pgas_rdma_endpoint_local(nq_pgas_rdma_node *n,nq_pgas_rdma_endpoint *out){
  if (!n || !out) return -EINVAL;
  std::lock_guard<std::mutex> lock(n->mu);
  *out = n->local;
  return 0;
}
int nq_pgas_rdma_connect(nq_pgas_rdma_node *n,const nq_pgas_rdma_endpoint *remote){
  if (!n || !remote || !remote->qp_number || !remote->rkey ||
      !remote->address || remote->slots == 0 || remote->slots > 1000000 ||
      remote->psn > 0x00ffffff) return -EINVAL;
  std::lock_guard<std::mutex> lock(n->mu);
  if (n->connected || n->unusable) return -EALREADY;
  ibv_qp_attr init{};
  init.qp_state = IBV_QPS_INIT;
  init.pkey_index = 0; init.port_num = n->port;
  init.qp_access_flags = IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
  int flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
  if (ibv_modify_qp(n->qp, &init, flags) != 0) { n->unusable = true; return -EIO; }
  ibv_qp_attr rtr{};
  rtr.qp_state = IBV_QPS_RTR;
  rtr.path_mtu = n->port_attr.active_mtu;
  rtr.dest_qp_num = remote->qp_number;
  rtr.rq_psn = remote->psn;
  rtr.max_dest_rd_atomic = 1;
  rtr.min_rnr_timer = 12;
  rtr.ah_attr.port_num = n->port;
  rtr.ah_attr.dlid = remote->lid;
  rtr.ah_attr.sl = 0;
  if (n->port_attr.link_layer == IBV_LINK_LAYER_ETHERNET) {
    if (!gid_has_bytes(remote->gid)) { n->unusable = true; return -EINVAL; }
    rtr.ah_attr.is_global = 1;
    std::memcpy(rtr.ah_attr.grh.dgid.raw, remote->gid, 16);
    rtr.ah_attr.grh.sgid_index = n->gid_index;
    rtr.ah_attr.grh.hop_limit = 1;
  } else if (!remote->lid) {
    n->unusable = true; return -EINVAL;
  }
  flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
          IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
  if (ibv_modify_qp(n->qp, &rtr, flags) != 0) { n->unusable = true; return -EIO; }
  ibv_qp_attr rts{};
  rts.qp_state = IBV_QPS_RTS;
  rts.timeout = 14; rts.retry_cnt = 6; rts.rnr_retry = 6;
  rts.sq_psn = n->local.psn; rts.max_rd_atomic = 1;
  flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
          IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
  if (ibv_modify_qp(n->qp, &rts, flags) != 0) { n->unusable = true; return -EIO; }
  n->peer = *remote; n->connected = true;
  return 0;
}
int nq_pgas_rdma_read(nq_pgas_rdma_node *n,uint32_t slot,
                       uint32_t timeout_ms,int64_t *value) {
  return submit(n, slot, false, timeout_ms, value);
}
int nq_pgas_rdma_write(nq_pgas_rdma_node *n,uint32_t slot,
                        int64_t value,uint32_t timeout_ms) {
  return submit(n, slot, true, timeout_ms, &value);
}
int nq_pgas_rdma_close(nq_pgas_rdma_node *n) {
  if (!n) return -EINVAL;
  destroy(n);
  return 0;
}
}
#else
extern "C" {
int nq_pgas_rdma_available(void) { return -ENOTSUP; }
int nq_pgas_rdma_open(uint32_t, uint8_t, uint8_t, nq_pgas_rdma_node **out) {
  if (out) *out = nullptr;
  return -ENOTSUP;
}
int nq_pgas_rdma_endpoint_local(nq_pgas_rdma_node *,nq_pgas_rdma_endpoint *) { return -ENOTSUP; }
int nq_pgas_rdma_connect(nq_pgas_rdma_node *,const nq_pgas_rdma_endpoint *) { return -ENOTSUP; }
int nq_pgas_rdma_read(nq_pgas_rdma_node *,uint32_t,uint32_t,int64_t *) { return -ENOTSUP; }
int nq_pgas_rdma_write(nq_pgas_rdma_node *,uint32_t,int64_t,uint32_t) { return -ENOTSUP; }
int nq_pgas_rdma_close(nq_pgas_rdma_node *) { return -ENOTSUP; }
}
#endif
