# Framework hardening increment — 26 October 2026

**Status: developer preview, NOT production-qualified.** This document describes a
bounded compatibility-preserving increment towards the eight framework goals.
Noqeri remains a general-purpose programming language. No Web/AI-only language
syntax was added. Existing examples and the ordinary CLI remain supported.

## Capability and verification matrix

| Goal | Implemented in this increment | Not yet qualified |
| --- | --- | --- |
| 1. Unified typed NIR for web | Existing Stage-0 typed NIR and native web front-end preserved; no artificial claim of convergence | Common typed web AST/NIR lowering, source maps and Stage-2 parity are **still missing** |
| 2. HTTP, TLS and concurrency | Read-only page/database requests render against a session snapshot outside the global mutex. Trusted HTTPS **reverse-proxy origin** configuration enforces Host, Origin, and Secure cookies. Server remains loopback-only | In-process TLS, certificate rotation, load/DoS benchmarks, bounded worker pool, comprehensive HTTP/2/3 interoperability |
| 3. Authn/authz | Configurable 1–3600-second session TTL, expiry checked on POST and GET, constant-time CSRF comparison, opt-in strict POST Origin, pre-existing Basic gate | User accounts, password hashing/rotation, MFA, per-route RBAC/ABAC, audited security model |
| 4. NoeSQL and typed data | Native session v2 callback dispatch is serialized; changes still wrap writes in begin/commit/rollback. SQLite already emitted typed JSON, and PostgreSQL now emits recognized null/bool/integer/numeric JSON; DuckDB and MySQL preserve null vs empty string | Canonical native NoeSQL real-provider ACID qualification, typed input bindings, cross-statement transactions, typed row templates, backend parity |
| 5. Browser E2E/security | Added real Chromium no-JS navigation, action, session isolation and XSS tests; separate loopback HTTPS-proxy policy and strict session-expiry regressions | Complete DAST, accessibility/visual regression, TLS handshake tests, third-party security assessment |
| 6. WebSocket | Bounded RFC6455 continuation reassembly across frames, including interleaved controls and whole-message UTF-8 validation | Actual WebSocket upgrade/handshake, socket read/write and TLS, ping lifecycle, close policy, pressure/backpressure and live interoperability |
| 7. Native ML | SGD now stages tensor updates and commits only when the entire result remains valid; added gradient-alias and overflow tests | Full transformer training ecosystem, accelerator kernels, distributed training, mixed precision, source-level Noqeri native tensor binding |
| 8. Packaging and APIs | Public header interface remains compatible; this page enumerates commands, qualifiers and packaging/release gates | Semver-stable framework ABI, conformance matrix on every platform, packaged framework SDK release |

Do not represent a subset passing CI as completion of these eight goals.

## Configuring TLS termination and strict session security

The Noqeri native web server intentionally binds only to 127.0.0.1 and does
not terminate TLS. A separately maintained reverse proxy must terminate TLS
and send traffic to the loopback server. Example deployment environment:

    NOQERI_WEB_PUBLIC_ORIGIN=https://app.example.com
    NOQERI_WEB_REQUIRE_ORIGIN=1
    NOQERI_WEB_SESSION_TTL_SECONDS=900

- Public origin must be an explicit https:// hostname, optionally including a
  port. The backend accepts that Host and Origin, not arbitrary proxy headers.
- The public-origin setting makes session cookies Secure automatically.
- Requests missing Origin are rejected on POST only when strict mode is 1.
  HTTP clients outside browsers may require an explicit Origin.
- A direct connection to the loopback backend is **not equivalent** to a TLS
  client connection. Restrict loopback accessibility and protect the proxy-to-
  backend boundary. Do not expose it on a public interface.
- Configure reverse-proxy connection/body limits, request timeouts, certificate
  issuance/renewal, HSTS, access logs, and upstream health checks. Noqeri does
  not currently automate those duties.
- Never deploy Basic credentials in source files; existing Basic authentication
  is a development gateway only, not application authorization.

For Caddy, a local HTTPS setup might use:

    app.example.com {
      encode zstd gzip
      header Strict-Transport-Security "max-age=31536000; includeSubDomains"
      reverse_proxy 127.0.0.1:8080 {
        header_up Host {host}
      }
    }

DNS and certificates must be provisioned according to the external proxy's
documentation; the example does not enable or verify TLS in this repository.

## Concurrency and storage contract

Native GET now copies the current session and persisted values under a short
mutex hold, then executes view database reads and renders HTML using that
snapshot without holding the global session mutex. A concurrent POST may commit
before the GET finishes, so rendering is snapshot-like from the application
state's perspective but database results are independently executed (not an
ACID read transaction across stores). POST mutations remain serialized and
keep the existing one-database-statement restriction.

A native NoeSQL v2 provider is guarded by a process-wide mutex until its
thread-safety contract is verified. This is a safety measure, **not** a pool.
The NoeSQL engine controls isolation, journaling and commit guarantees; the mock
ABI test establishes callback wiring only, not those guarantees.

Native query results are bounded JSON: SQL NULL is represented as JSON null,
not the empty string. PostgreSQL known boolean and numeric types emit JSON
primitives; other provider types are safely escaped as text. Native NoeSQL
v2 binding values are still text strings in a JSON object; typed bind tags
and typed NoeSQL schema rows require a coordinated ABI update with the
upstream NoeSQL project.

## Framework API and packaging rules

- Keep the primary language/compiler in the Noqeri repository.
- Treat the Stage-0 web parser and self-hosted compiler as separate until they
  pass the same grammar, typed IR, diagnostics and codegen conformance corpus.
- Version native runtime C ABI contracts explicitly; reject mismatches rather
  than silently guessing. NoeSQL session v2 already checks abi_version=2.
- Keep packaged Noqeri source imports, headers, native libs and transitive ABI
  dependencies visible. Do not publish a new “stable framework ABI” until a
  documented compatibility test runs on Linux, Windows and macOS.
- The WebSocket MessageAssembler is an in-memory frame API, not a transport
  service. Do not advertise a live WebSocket server without handshake/socket
  tests.
- Do not make ordinary general-purpose Noqeri programs depend on Node, browser
  engines or the optional AI framework library.

## Regression commands

    cmake -S . -B build -DBUILD_TESTING=ON
    cmake --build build --target noqeri-web noqeri-websocket-frame-test noqeri-ai-framework-test
    ctest --test-dir build -R '^noqeri-(native-web|websocket-frame|ai-framework)$' --output-on-failure
    python3 tests/native_web_actions.py ./build/noqeri-web examples/native_web_actions
    python3 tests/native_web_session_expiry.py ./build/noqeri-web examples/native_web_actions
    python3 tests/native_web_proxy_origin.py ./build/noqeri-web examples/native_web_actions

The optional browser workflow installs Playwright **for testing only**, and
executes:

    node tests/browser_e2e.mjs ./build/noqeri-web ./examples/native_web_actions

A successful test run covers only the explicitly asserted configurations.
For production qualification, also require independent HTTPS handshake and
certificate rotation testing, sustained concurrent load, real NoeSQL
transaction/rollback and injection tests, protocol fuzzing, and security review.
