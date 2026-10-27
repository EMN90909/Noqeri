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
- Native compilation of `input on.input` / `on.change` using
  `state = form.value` or `state = form.checked`, and of
  `form on.submit` with bounded, named `form.field` assignments.
  The browser runtime keeps form submission in-page; server-side DB and
  privileged actions still require `serve-app`.
- Bounded same-origin `load resource from "/api/data.json"` data loading,
  including safe own-property access (`resource.loading`, `resource.data`,
  `resource.error`, and nested data properties). The C++ compiler generates
  browser fetch requests with abort/disposal handling supplied by the runtime;
  Node.js is not used during compilation.
  **Async-only views cannot be statically pre-rendered with unknown data**:
  the HTML uses a client mount point and a `noscript` message instead.
  Host the JSON endpoint on the same origin; generated builds do not create
  or fetch server APIs.
- Browser-only `subscribe updates from "/ws/updates"` with a fixed same-origin
  WebSocket path, reconnecting JSON messages, bounded frames, and automatic
  cleanup on app disposal. A separate WebSocket server must implement the
  endpoint; native `serve-app` has no WebSocket upgrade handling.
- `on.mount { state = "ready" }` for the same bounded, mutable-state
  assignments allowed by native DOM events. Effects run once when the browser
  mounts the view, not on the server; arbitrary function or JS calls remain
  unsupported. Pages with subscriptions use client rendering rather than
  incorrectly prerendering missing realtime values.
- The runtime's existing bounded HTML/URL/attribute validation. Unsupported
  actions fail the build rather than silently becoming client-side powers.

**Not yet supported by native browser codegen:** arbitrary functions and
expression operators, JS imports, third-party packages, general-purpose
`on.mount` effects, advanced schema/form validation, and typed Stage-2 compiler/NIR
parity. Native async JSON loads and primitive input/form events are supported,
but do **not** yet cover all forms of the experimental browser-view grammar. These features remain with the separate
experimental Node-hosted view compiler. Unlike `serve-app`, `build-client`
does not execute server database actions or privileged form handlers.

Browser-persisted values are only for nonsecret preferences. Never store auth
credentials or private records in `persist`.

## Async native browser-view example

```noqeri
let username = "Guest"
load greeting from "/api/greeting.json"

view {
  main {
    h1 { "Greetings" }
    p { "Message: " + greeting.data.message }
    p { "Name: " + username }
    input on.input { username = form.value } {}
    form on.submit { username = form.username } {
      input type: "text" name: "username" {}
      button type: "submit" { "Save" }
    }
  }
}
```

Build with `noqeri web build-client <source> <dist>`, no Node.js required.
A same-origin JSON endpoint must actually exist at `/api/greeting.json`.
Fetches have a timeout and bounded response size. The compiler rejects
protocol-relative/external URLs, traversal syntax, member access to special
JavaScript prototype properties and unsupported client action kinds. Browser
event state is a local UI convenience only. Never treat it as trusted server
identity or write authorization.

Run `python3 tests/native_web_client_async.py ./build/noqeri-web` to
qualify no-Node compilation and rejection cases. CI also runs a real Chromium
test for fetching JSON, typed inputs, and in-page form submissions.

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

### Strict default-deny authorization

The route policy normally leaves paths not listed in the policy public for
backward compatibility. On a restricted deployment, turn on explicit
**default deny** to prevent accidental exposure of new routes:

```sh
export NOQERI_WEB_AUTHZ_FILE=/etc/noqeri/route-access.txt
export NOQERI_WEB_AUTHZ_DEFAULT_DENY=1
```

With this setting, an uncovered application path returns 403 even when a
request supplies a valid credential for a different path. A missing policy
file or values other than `0`/`1` are startup errors. Only the
read-only framework assets `/theme.css` and `/__nq/reactive.js` remain
accessible without a bearer header because ordinary browsers cannot attach
one to stylesheet or script requests. They contain no user state.
Every private page and API route still needs a matching explicit grant.
This is not a replacement for per-account, per-object authorization or a
full identity provider.

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
