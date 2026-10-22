# Experimental mailbox runtime primitives

This adds **Linux/POSIX C ABI building blocks**, not a completed SMTP, IMAP,
TLS, or Noqeri standard-library implementation. A compiler-supported `extern`
declaration can link against `noqeri_mailbox_runtime` when the native linker
and C ABI support are configured; this integration has **not** been exercised.

## Components

- `nq_mail_listen_loopback`, `nq_mail_connect_loopback`, `nq_mail_accept`:
  IPv4 **loopback only** sockets for test harnesses, with bounded accept/connect
  timeouts. Reads/writes are synchronous and may block. The host application
  owns descriptors. Never use this adapter as an Internet-facing listener.
- `nq_mail_workers_*`: bounded queued tasks with graceful draining.
  Worker tasks must not call shutdown from within a worker. Queue capacity
  bounds *waiting jobs*, not callback memory or the number of running jobs.
- `nq_mail_pool_*`: allocation quota with ownership checking and leak
  detection via `destroy == -EBUSY`. Caller must finish all threads using a
  pool before destroy; concurrent destruction is undefined.
- `nq_mail_atomic_write`: writes a temporary file in an existing directory
  with mode 0600, fsyncs it, renames it over the basename, and fsyncs the
  parent directory. This does not make multi-record transactions atomic,
  enforce mailbox uniqueness, or support crash-safe network acknowledgements
  by itself. Writes replacing a preexisting file discard its old mode/owner.

## Build and test (Linux)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DNOQERI_ENABLE_SANITIZERS=ON
cmake --build build --target noqeri-mailbox-runtime-test
ctest --test-dir build -R '^noqeri-mailbox-runtime$' --output-on-failure
```

No network calls, mail delivery, compiler self-hosting, SMTP/IMAP parsing,
TLS, NoeSQL integration, or external production listener are included.

## Remaining requirements before NoeMailer launch

1. Wire C ABI functions into a linked native Noqeri executable and assert
   the full compilation + execution path in CI.
2. Nonblocking sockets and an epoll/kqueue/IOCP reactor, cancellation,
   connection caps, deadlines, and per-client backpressure.
3. TLS 1.2+/1.3, STARTTLS state handling, certificate rotation, protocol
   parsing limits, authentication/rate limits, and abuse defenses.
4. Durable mail transactions: unique message IDs, staged writes,
   mailbox index commit/recovery, fsync acknowledgement boundaries,
   quota policy, cross-process locking, and fault injection.
5. Sanitizer + ThreadSanitizer stress tests; thousands of concurrent clients;
   Linux/macOS/Windows compatibility, and Noqeri native runtime validation.

The local authoring environment could not clone GitHub; the new target is
committed as source and must still be built and executed in a full checkout.
