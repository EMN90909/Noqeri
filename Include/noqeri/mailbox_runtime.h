#ifndef NOQERI_MAILBOX_RUNTIME_H
#define NOQERI_MAILBOX_RUNTIME_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* All functions return nonnegative results on success, negative errno-style
 * error codes on failure. File descriptors returned by these APIs are owned
 * by the caller and must be closed through nq_mail_close. POSIX only. */
int nq_mail_listen_loopback(uint16_t port, int backlog);
int nq_mail_accept(int listener, int timeout_ms);
int nq_mail_connect_loopback(uint16_t port, int timeout_ms);
int64_t nq_mail_read(int fd, void *buffer, size_t capacity);
int64_t nq_mail_write(int fd, const void *buffer, size_t size);
int nq_mail_close(int fd);
int nq_mail_local_port(int fd);
/* Atomically replace a single basename inside an existing directory.
 * The caller must enforce its own logical transaction and deduplication.
 * A successful call fsyncs the new file and parent directory. */
int nq_mail_atomic_write(const char *directory, const char *basename,
                         const void *data, size_t size);
/* Quota-limited allocation. Allocated blocks must be freed with the same pool.
 * Destroying a pool with outstanding allocations fails with -EBUSY. */
typedef struct nq_mail_pool nq_mail_pool;
nq_mail_pool *nq_mail_pool_create(size_t max_bytes);
void *nq_mail_pool_alloc(nq_mail_pool *, size_t bytes);
int nq_mail_pool_free(nq_mail_pool *, void *);
int nq_mail_pool_destroy(nq_mail_pool *);
size_t nq_mail_pool_used(nq_mail_pool *);
/* Bounded worker queue. Submit returns -EAGAIN when full; shutdown drains
 * work before returning. Never call shutdown from inside the pool callback. */
typedef struct nq_mail_workers nq_mail_workers;
typedef void (*nq_mail_job_fn)(void *);
nq_mail_workers *nq_mail_workers_create(size_t threads, size_t queue_limit);
int nq_mail_workers_submit(nq_mail_workers *, nq_mail_job_fn, void *);
int nq_mail_workers_shutdown(nq_mail_workers *);
#ifdef __cplusplus
}
#endif
#endif
