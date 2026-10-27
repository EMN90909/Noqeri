# Native Noqeri browser compilation and application security

This is a verified-in-stages migration from the experimental Node-hosted
browser-view compiler to the C++ native bootstrap. Neither a complete port of
every browser-view language feature nor a promise of a public production HTTP
server is implied by these additions.

## Build a reactive client-side website without Node.js

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target noqeri noqeri-web -j2

# Both commands call the C++ renderer, not Node or npm.
./build/noqeri web build-client examples/native_web_client dist/native-client
# Equivalent:
./build/noqeri-web build-client examples/native_web_client dist/native-client

# Preview only. A production static-file server/CDN is recommended for deployment.
./build/noqeri-web serve dist/native-client 8080
```

The native `build-client` path generates browser-standard `main.mjs`,
`__noqeri/runtime.mjs`, CSS and static-prerendered `index.html` pages.
Its JavaScript is **generated** by the native C++ bootstrap and **runs only in
browsers**; neither building nor hosting those files requires Node.js.

Current accepted Noqeri subset:

- `let` and `const` state, `persist` primitive state (browser localStorage);
  static expressions, strings and bounded integer-style increments.
- Literal routes, a fallback `view`, reusable components and standard HTML
  element declarations using Noqeri syntax.
- Client-side `button on.click`, reactive DOM reconciliation, browser history,
  prerendered first paint and no-JS reading of the initial static HTML.
- The runtime's existing bounded HTML/URL/attribute validation. Unsupported
  actions fail the build rather than silently becoming client-side powers.

**Not yet supported by native browser codegen:** arbitrary functions and
expression operators, `load` with async data, WebSocket `subscribe`, JS
imports, third-party packages, `on.mount`, general input/form events, and
typed Stage-2 compiler/NIR parity. These features remain with the separate
experimental Node-hosted view compiler. Unlike `serve-app`, `build-client`
does not execute server database actions or privileged form handlers.

Browser-persisted values are only for nonsecret preferences. Never store auth
credentials or private records in `persist`.

## Protected routes (API clients)

A native full-stack server can require distinct **bearer** credentials for
specific path prefixes and HTTP methods. The policy file contains **environment
variable names, not the tokens**:

```text
# METHOD ROUTE_PREFIX SECRET_ENV
GET /members NOQERI_MEMBER_VIEWER_TOKEN
BOTH /members NOQERI_MEMBER_EDITOR_TOKEN
BOTH /admin NOQERI_ADMIN_WRITE_TOKEN
```

```sh
export NOQERI_WEB_AUTHZ_FILE=/etc/noqeri/route-access.txt
export NOQERI_MEMBER_VIEWER_TOKEN='<random secret at least 32 characters>'
export NOQERI_MEMBER_EDITOR_TOKEN='<different random secret at least 32 characters>'
export NOQERI_ADMIN_WRITE_TOKEN='<different random secret at least 32 characters>'
./build/noqeri-web serve-app examples/native_web_fullstack 8080
```

A `POST` grant requires a matching `GET` or `BOTH` grant for the same
principal and path, because native actions first acquire a CSRF token via GET.
The most specific path prefix wins. Routes not covered by any policy are
**public**, so enumerate every private prefix. Under a protected prefix,
unauthenticated requests and ungranted HTTP methods are denied. Each accepted
principal is bound to its cookie-backed CSRF session: a cookie cannot be reused
by a second principal. The policy refuses invalid formats, missing secrets,
short secrets and combinations with the global Basic/Bearer gates.

This is deliberately **API-style** access control: ordinary browser navigation
does not attach a bearer header. For actual user-facing websites, deploy a
tested identity provider and user/tenant-scoped authorization enforcement,
rather than treating these shared static tokens as end-user accounts. These
tokens must be provisioned via a secrets manager and rotated operationally.

## Shared session backend on one host

```sh
# Must use a build with the optional NOQERI_WEB_SQLITE driver enabled.
export NOQERI_WEB_SESSION_SQLITE=/var/lib/noqeri/web-sessions.sqlite
# Use the same path from multiple noqeri-web workers on the SAME machine.
./build/noqeri-web serve-app examples/native_web_sessions 8080
```

When enabled, session values, principal identities and rotating CSRF tokens
are stored in a **private 0600 SQLite database** using WAL, prepared statements
and write transactions. Multiple workers sharing that database observe each
other's sessions, serialized state updates, single-use CSRF rotation and
session expiry. The backend rejects symlinked or excessively permissive session
files and removes expired rows as new sessions are issued.

The backend **disallows global `persist` state backed by process-local
`.nqkv`**. This prevents accidentally presenting non-coordinated cross-worker
writes as durable shared state. Use an independently qualified transactional
database for shared application records.

SQLite WAL requires a **local filesystem with proper POSIX locking**. Do not
mount it on NFS or assume it shares state across different machines or regions.
A full cross-host store (for example a tested Redis/SQL session service with
TLS, atomic compare-and-swap, migrations, backup/recovery and failover) is a
**remaining milestone**. Use this SQLite mode for local multi-worker staging
and testing, not a substitute for distributed production architecture.

## CI and deployment gates

Native CI tests Node-free compilation, static output, client and persisted
browser state, session/CSRF isolation, two SQLite-backed processes, request
bounds, and route permissions. Chromium tests SSR hydration and route navigation
with no document reload; the experimental Node compiler still has its own
separate regression suite.

Deploy public traffic only behind a qualified HTTPS ingress. The
[Nginx template](../Deploy/nginx/noqeri-native-https.conf.example) is an
example, not a complete account-security solution. Before release, test TLS,
spoofed `Host`/`Origin`, request smuggling, actual proxy timeouts, authenticated
rate limits, identity/tenant isolation, slow clients, and p95/p99 latency under
realistic production traffic. Add structured security events, error budgets,
session-store monitoring, rollback and restore drills.

### Production ingress smoke test

Run the read-only HTTPS security and latency test against the **actual**
staging/production URL with a publicly trusted certificate:

```sh
python3 tests/native_web_deployment_smoke.py https://example.com \
  --protected /members --samples 64 --workers 8 --max-p95-ms 1500
```

The script verifies TLS certificate trust, HSTS, frame protections, nosniff,
anonymous access denial for the selected protected path, absence of HTTP
downgrades, and real end-to-end concurrent p50/p95 latency. For private CA
staging, use `--ca-file staging-ca.pem` instead of disabling verification.
The test does not submit mutations or manufacture accounts.

The HTTPS smoke test **cannot run automatically against a real deployment**
until a deployment URL, cert trust and test authorization boundary are
configured. A passing localhost benchmark is not a substitute.

See [web production readiness](WEB_PRODUCTION_READINESS.md) for the release
matrix and existing benchmarks.
