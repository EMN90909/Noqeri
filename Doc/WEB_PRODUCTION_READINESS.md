# Noqeri Web: release readiness and performance qualification

This guide distinguishes working, tested capabilities from production certification.
Passing CI is necessary, but does **not** certify the built-in preview HTTP
server for public Internet exposure or make the browser DSL compiler self-hosted.

## Choose the correct compiler and runtime

| Mode | App source | Build host | Browser experience | Deployment status |
| --- | --- | --- | --- | --- |
| Native static | `.noqeri` routes and UI | C++17 compiler / `noqeri web build`, **no Node.js** | Pre-rendered HTML/CSS, no client JS | Static output can be hosted on standard static hosting |
| Native full stack | `.noqeri` routes, forms and actions | C++17 compiler / `noqeri web compile`, **no Node.js** | Native SSR, accessible HTML POST fallback; optional `NOQERI_WEB_REACTIVE=1` for hydrated actions and SPA-style same-origin navigation | **Preview only** without independently qualified authentication, reverse-proxy, TLS and operations |
| Native browser client subset | `.noqeri` views, components, literal routes, client `on.click` and `persist` | C++17 compiler / `noqeri web build-client`, **no Node.js** | Static prerender, browser hydration/CSR, client-side routing and nonsecret local preferences | Generated files suitable for static hosting; compiler subset is still experimental |
| Experimental browser views | `.noqeri` view DSL | `Runtime/web-project.mjs`, **Node.js 22+ build-time dependency** | Browser ES modules, CSR and safe static route pre-render/hydration | Experimental, not yet the typed Stage-2 native frontend |

**The experimental Node-hosted browser-view compiler is not yet fully ported.**
The native `build-client` subset does generate browser modules without Node,
but does not support all features of that experimental compiler. Noqeri-authored
source and no handwritten JavaScript do not mean the compiler is self-hosted
or written entirely in Noqeri itself.
Node.js is not required by the native static, native browser-client or native
full-stack paths, nor by browsers consuming their output.

For precise supported syntax, limits, route authorization and shared session
configuration, see [Native client compilation and security](WEB_NATIVE_CLIENT_SECURITY.md).

### Standalone Noqeri view, now supported by the native compiler

A Noqeri program containing **only** a `view { ... }` declaration no longer
needs to wrap its page in `route "/" { ... }` to enter native static or
server-rendered mode. For example:

```noqeri
let clicks = 0
view {
  main {
    h1 { "Hello from Noqeri" }
    p { "Clicks: " + clicks }
    button on.click { clicks = clicks + 1 } { "Increment" }
  }
}
```

`noqeri web serve-app <project>` executes this as a native SSR page and
optionally enhances it in the browser with `NOQERI_WEB_REACTIVE=1` (hydrated
server actions and same-origin client navigation). The native static builder
also accepts **view-only pages without events**; it deliberately rejects
interactive mutations rather than silently emitting dead buttons.

This shares user-facing Noqeri syntax and builds with **no Node.js**, but its
reactivity executes **server-side**. The separate experimental `.mjs`
compiler supports client-side reactive updates and imports/loads that the
native compiler does not. This overlap is **not** a claim that the browser DSL
has been fully ported or that the compiler is written in Noqeri itself.

## Performance and security gates

GitHub Actions now checks all of the following:

- Native Release-mode build, native compiler/tests, route rendering, session
  isolation, CSRF rotation/replay protection, malformed HTTP, optional Basic /
  bearer authentication, database adapters and proxy origin boundaries.
- Real Chromium tests for hydrated browser view text reconciliation (including
  adjacent text nodes coalesced by the HTML parser), DOM preservation and
  client-side routing. Native view-only builds and actions are tested while
  Node.js is removed from PATH.
- Strict CRLF-only HTTP header framing, at most 64 headers, bounded request
  sizes and a four-second absolute request deadline across headers and body.
  Targeted tests cover malformed line endings, TE/CL conflicts and slowly
  trickled requests.
- Native client module emission and real Chromium checks without a Node build
  requirement; state updates, route navigation and persisted browser preferences.
- Route-scoped bearer credentials with method restrictions and principal-bound
  CSRF sessions. Optional SQLite WAL transactional sessions are tested across
  two local processes, including replay and server restart. This is **not**
  a cross-host distributed session service.
- Optional `NOQERI_WEB_RATE_LIMIT_PER_MINUTE` throttles each authorized
  principal independently. The strict 1..10,000 requests/minute configuration
  fails closed, and exhausted quotas return HTTP 429. An anonymous preview
  principal shares one quota; unique-principal records are memory bounded.
  **This is per-process enforcement**, not cluster-wide distributed throttling;
  production traffic also needs ingress/shared-rate limits.
- Native HTTP loopback performance in **both** JavaScript-disabled and hydrated
  server rendering: 120 measured sequential GETs + 120 GETs at 8-client
  concurrency, reusing a session and making fresh HTTP/1.1 connections.
- Default **p95 GET under 250 ms**, 8-client **p95 under 500 ms**, and
  **8-client throughput over 60 requests/s** on the shared runner. These are
  intentionally conservative CI regression gates, **not** advertised product
  throughput or cloud hosting SLOs.
- Chromium browser E2E for no-JavaScript fallback, SSR hydration, native form
  POSTs, client-side routing and browser history, HTML escaping, and no
  duplicated document navigation. Client navigation p95 must be **under 800
  ms**, measured across 32 real local same-origin requests and reconciliations.
- Experimental browser-view build benchmark of 160 assets and 4 runs per mode
  comparing serial (`NOQERI_WEB_BUILD_CONCURRENCY=1`) and bounded batched
  (`=8`) builds. The p95 budget for each is **under 3,000 ms**.
  Relative speedup is reported, **not asserted**: parallel I/O is not always
  faster on constrained disks.

**Measurements must come from completed jobs**, not merely queued workflows.
Check the Native web, Noqeri reactive web views and Browser E2E jobs under
[GitHub Actions](https://github.com/EMN90909/Noqeri/actions) for the exact
commit being released. A new commit makes previous jobs' results historical.

## Build and verify

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-release --target noqeri noqeri-web --parallel 2
ctest --test-dir build-release -R '^noqeri-native-web$' --output-on-failure

# Fully Node-free static output:
./build-release/noqeri web build examples/native_web_static dist/site

# Node-free compiler emits browser modules with real CSR/hydration:
./build-release/noqeri web build-client examples/native_web_client dist/browser

# Server-side Noqeri actions with optional browser progressive enhancement:
NOQERI_WEB_REACTIVE=1 ./build-release/noqeri web serve-app examples/native_web_actions 8080

# Node-free HTTP microbenchmark on loopback only:
python3 tests/native_web_performance.py ./build-release/noqeri-web examples/native_web_actions

# Separate, experimental Node-hosted view compiler qualification:
node --test tests/web-hydration.test.mjs tests/web-views.test.mjs
node tests/web-build-performance.mjs
```

Tighten budgets and test more demanding workloads on intended deployment
hardware. The internal native preview server binds to `127.0.0.1`; no
public-facing/TLS deployment is qualified by these tests.

### Reference HTTPS reverse proxy

A sample [Nginx HTTPS configuration](../Deploy/nginx/noqeri-native-https.conf.example)
documents request and connection limits, buffering, restricted POST bodies,
TLS termination and HSTS. Update the hostname, certificates and per-site
traffic limits; test with `nginx -t` and independent penetration/load checks.
Do not bind the native process to a public network interface.

For a proxied app, configure `NOQERI_WEB_PUBLIC_ORIGIN=https://example.com`
so Host/Origin validation uses the public HTTPS URL and the server issues
`Secure` session cookies. Set `NOQERI_WEB_REQUIRE_ORIGIN=1` for clients
that can provide mandatory browser Origin on POST. Put user authentication and
fine-grained authorization in front of or inside each app; a valid Noqeri CSRF
token is not equivalent to an authenticated account.

**Even with the sample proxy, native actions remain preview-stage** until
application authorization, rate limiting by authenticated identity,
multi-instance sessions, complete proxy interoperability and production
operations are qualified.

## Required before promoting native actions or browser views to GA

1. Replace or wrap the preview HTTP implementation with a production-qualified
   HTTP/1.1/HTTP/2 ingress, configured reverse proxy/TLS termination, client
   authentication and per-route authorization. Validate header forwarding,
   connection limits and HTTP request smuggling on the actual proxy stack.
2. Add browser E2E coverage on Firefox and Safari/WebKit, including mobile,
   keyboard navigation, accessibility and controls with focus/selection/IME.
3. Implement application-level rate limiting, auditable request tracing,
   structured error telemetry, load/shedding and uptime/rollback procedures.
4. Test p50/p95/p99 response and interactive latency under the intended real
   workload, including network RTT, CSS/JS cold-cache and slow devices.
5. Optional SQLite WAL sessions now allow **same-host workers** to share CSRF,
   identity and ephemeral state. Complete a truly distributed TLS-enabled
   session backend, cross-host atomic updates, recovery/load tests and tenant
   consistency qualification before running across machines or regions.
6. For Node-free **browser-view DSL** builds specifically, port the view parser,
   typed checking, package resolution and safe JavaScript/WASM code emission
   into the native or self-hosted Noqeri compiler. Verify byte-for-byte or
   semantics-equivalent output against the existing tests before removing
   the Node.js experimental tool. This milestone remains **open**.

## Browser safety model

Native HTML responses and private state are `Cache-Control: no-store`. The
native theme CSS and framework JS have `private, max-age=300,
must-revalidate`, which permits safe browser-local reuse without shared proxy
caching. Response headers include CSP with `frame-ancestors 'none'`, same-origin
resource policy and `X-Frame-Options: DENY`. The reactive script only connects
to same-origin HTML routes; all form mutations remain server-side with
route-scoped action IDs and rotating CSRF tokens.

Links: [Native actions](WEB_NATIVE_ACTIONS.md),
[Native client and security operations](WEB_NATIVE_CLIENT_SECURITY.md),
[Web framework](WEB_FRAMEWORK.md),
[Qualification matrix](WEB_FRAMEWORK_QUALIFICATION.md).
