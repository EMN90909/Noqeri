#ifndef NOQERI_MAILBOX_EXTENDED_H
#define NOQERI_MAILBOX_EXTENDED_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Linux epoll reactor. Registered socket descriptors remain caller-owned.
 * Level-triggered events: READ=1, WRITE=2, ERROR=4.
 * No callback runs inside wait; the caller handles backpressure. */
typedef struct nq_mail_reactor nq_mail_reactor;
typedef struct { int fd; uint32_t events; } nq_mail_event;
nq_mail_reactor* nq_mail_reactor_create(void);
int nq_mail_reactor_add(nq_mail_reactor*, int fd, uint32_t events);
int nq_mail_reactor_modify(nq_mail_reactor*, int fd, uint32_t events);
int nq_mail_reactor_remove(nq_mail_reactor*, int fd);
int nq_mail_reactor_wait(nq_mail_reactor*, nq_mail_event* out, int capacity, int timeout_ms);
void nq_mail_reactor_destroy(nq_mail_reactor*);
int nq_mail_set_nonblocking(int fd);
/* OpenSSL-based TLS; credentials and CA are PEM paths.
 * The TLS context owns keys; accepted sessions own their SSL objects but not sockets.
 * TLS operations return bytes, or negative error codes; -EAGAIN means retry when
 * the corresponding socket becomes ready. Caller must serialize session access.
 * Handshake does not implement a deadline: use nonblocking sockets and reactor.
 */
typedef struct nq_mail_tls_context nq_mail_tls_context;
typedef struct nq_mail_tls_session nq_mail_tls_session;
nq_mail_tls_context* nq_mail_tls_server(const char* cert, const char* key);
nq_mail_tls_context* nq_mail_tls_client(const char* ca_file);
nq_mail_tls_session* nq_mail_tls_attach(nq_mail_tls_context*, int fd, const char* hostname);
int nq_mail_tls_handshake(nq_mail_tls_session*);
int64_t nq_mail_tls_read(nq_mail_tls_session*, void*, size_t);
int64_t nq_mail_tls_write(nq_mail_tls_session*, const void*, size_t);
int nq_mail_tls_want_events(nq_mail_tls_session*);
void nq_mail_tls_session_destroy(nq_mail_tls_session*);
void nq_mail_tls_context_destroy(nq_mail_tls_context*);
/* Durable single-message commit. Only letters, digits, '-' and '_' are
 * accepted in message IDs. No overwrite is permitted. On success message
 * data and directory entry have been fsynced; return -EEXIST for duplicate ID.
 * A mailbox index must be rebuilt from committed files after a crash. */
int nq_mail_commit_message(const char* directory, const char* id,
                           const void* data, size_t size);
#ifdef __cplusplus
}
#endif
#endif
