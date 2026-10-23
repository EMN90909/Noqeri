#ifdef NDEBUG
#undef NDEBUG
#endif
#include "noqeri/pgas_rdma.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
int main() {
  const int available = nq_pgas_rdma_available();
  assert(available == -ENOTSUP || available == 0 || available == 1);
  nq_pgas_rdma_node *node = nullptr;
  assert(nq_pgas_rdma_open(0, 1, 0, &node) < 0);
  assert(node == nullptr);
  // No active RDMA fabric: must explicitly report ENODEV rather than
  // pretending that an ordinary TCP socket provides RDMA semantics.
  if (available == 0) {
    assert(nq_pgas_rdma_open(8, 1, 0, &node) == -ENODEV);
    assert(node == nullptr);
  }
  if (available == -ENOTSUP) {
    assert(nq_pgas_rdma_open(8, 1, 0, &node) == -ENOTSUP);
  }
  // With available NICs, connection and completion tests are intentionally
  // separate physical-fabric qualifications, not hidden by this probe.
  std::puts("PGAS RDMA provider absent/device capability detection: PASS");
  return 0;
}
