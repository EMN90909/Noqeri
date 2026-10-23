#ifndef NOQERI_PGAS_PARTITION_H
#define NOQERI_PGAS_PARTITION_H
#include <stdint.h>
#include "pgas_runtime.h"
#include "pgas_tls_gateway.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Stage-0 native @partition integration: fixed i64 arrays only.
 * Applications must explicitly bind an ordered array of authenticated node
 * clients before calling noqeri_entry(). No credentials are read from source.
 * Connections remain caller-owned and must outlive the binding.
 * Unbind before closing clients; concurrent unbind/use is serialized.
 * This is native AOT integration, not Stage-2 self-hosted parity.
 */
int nq_pgas_partition_bind(const char *symbol, uint64_t elements,
    uint32_t nodes, uint32_t strategy, nq_pgas_client *const *clients);
/* Alternative binding for authenticated cross-host TLS sessions. Each TLS
 * connection must already have verified its expected server certificate DNS.
 * Exactly one transport can bind a symbol. A binding is immutable until
 * unbound; remote failures always fail closed instead of falling back to
 * plaintext or selecting another node.
 */
int nq_pgas_partition_bind_tls(const char *symbol, uint64_t elements,
    uint32_t nodes, uint32_t strategy, nq_pgas_tls_client *const *clients);
int nq_pgas_partition_unbind(const char *symbol);
/* These are called by compiler-generated native assembly, not application
 * code. Remote failures, unbound partitions and out-of-range indices fail
 * STOP (abort) rather than returning silently incorrect array data.
 */
int64_t nq_pgas_compiler_partition_read(const char *symbol, int64_t index,
    uint64_t elements, uint32_t nodes, uint32_t strategy);
int64_t nq_pgas_compiler_partition_write(const char *symbol, int64_t index,
    uint64_t elements, uint32_t nodes, uint32_t strategy, int64_t value);
#ifdef __cplusplus
}
#endif
#endif
