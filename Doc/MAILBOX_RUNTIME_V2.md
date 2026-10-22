# Noqeri mailbox runtime v2, experimental host ABI

This branch adds a **Linux-only C++ host adapter** to the Noqeri ecosystem,
not a self-hosted Noqeri server or completed standard library. Changes require
native build and execution validation. No performance claims are established.

## Interfaces

- `mailbox_runtime.h`: loopback TCP, a bounded worker queue, memory quotas,
  and replacement-style fsync writes (v1).
- `mailbox_extended.h`: Linux epoll readiness monitoring, nonblocking descriptor
  configuration, OpenSSL 1.1.1+/3.x TLS contexts and sessions, and durable single
  message commits.
- The TLS client requires certificate verification and checks the supplied
  hostname. The server loads a PEM certificate chain and private key. Callers
  must set sockets nonblocking and drive WANT_READ/WANT_WRITE using the reactor.
  STARTTLS protocol upgrades, revocation policy, certificate rotation, timeout
  cancellation, and TLS session ownership across event-loop workers are **not
  provided**.
- `nq_mail_commit_message` writes a temporary 0600 file, fsyncs its contents,
  publishes with no-overwrite hard-link, fsyncs the directory, unlinks pending,
  and fsyncs the directory again. A committed `ID.msg` is a durable message
  record, recoverable by scanning committed files. The API explicitly rejects
  duplicate IDs. Mailbox indexing, cross-message atomicity, transaction
  isolation, deletion, quota accounting, and crash-fault simulation remain work.

## Tests

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target noqeri-mailbox-runtime-test noqeri-mailbox-extended-test noqeri-mailbox-tls-test noqeri-mailbox-load-test noqeri
ctest --test-dir build -R '^noqeri-mailbox' --output-on-failure
node scripts/verify-mailbox-native-link.mjs build/noqeri build/libnoqeri_mailbox_runtime.a
```

The link acceptance test is **fail-closed** and must not be claimed to pass
until the native Noqeri compiler correctly compiles the extern fixture and the
C++ linker resolves and executes the ABI symbols.

For larger optional load simulation:

```sh
./build/noqeri-mailbox-load-test 5000
```

Ensure `ulimit -n` exceeds 2x requested connections. This load test measures
epoll readiness on local socketpairs, **not** real TLS handshake concurrency,
SMTP throughput, IMAP latency, Internet behavior, or multi-process crashes.

## Release blockers

- Run build, CTest, native link test, ASan/UBSan and separate TSan builds
  (do not combine TSan with ASan).
- Integrate API through a stable Noqeri module with verified native ABI call
  conventions, pointer types, ownership, and linker flags.
- Implement connection limits, fair scheduling, shutdown/cancellation, timeout
  propagation and write buffering, then measure with slow clients.
- Verify certificates and STARTTLS interoperability with external mail clients.
- Implement mailbox index/NoeSQL transactions, recovery and fault injection.
- Cross-platform backends (kqueue, IOCP), production monitoring and fuzzing.

Until these gates pass, keep the PR in draft.
