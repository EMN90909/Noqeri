# Noqeri framework qualification: native web, UI, HTTP, WebSocket

This is a framework-integration increment, **not a declaration of production readiness**. It improves the existing core frameworks and preserves explicit boundaries for NoeSQL and other databases. No duplication of the built-in UI or native wire helpers is needed in `noqeri-registry` yet.

| Subsystem | Verified improvement | Remaining gate |
| --- | --- | --- |
| Noqeri browser Web | Static route pre-rendering, safe HTML serialization, matching-node hydration, preserved reactive elements, repeatable build p50/p95 benchmark | Native/Stage-2 compilation of browser DSL, per-request SSR/data loading, keyed diffing, Firefox/WebKit/mobile qualification |
| Noqeri Native Web | Added expanded reusable components to JS-free static example; built and checked | SSR data loaders, diagnostics/source maps, incremental builds |
| Noqeri Native Actions | Verified CSP/connect-src, form charset, private HTML cache controls, strict resource headers, Chromium progressive hydration, no-reload client navigation and concurrency regression budgets | TLS/proxy interoperability, production HTTP parser and multi-user authentication/authorization, distributed session persistence, WebKit/Firefox |
| Noqeri UI | PageHeader, SectionHeading, Panel, EmptyState, FieldHint, FieldError, SecondaryLink, FooterNote, SubmitButton, TextAreaField | ARIA audit, keyboard/navigation tests, contrast and mobile visual regression |
| Noqeri Native Database | `noqeri-web drivers` enumerates compiled providers; mixed database and nontransactional state writes fail closed | Native NoeSQL E2E, transaction coordinator, typed result rows |
| Noqeri HTTP | Fixed HTTP no-body status helper and hardened native app HTTP request parsing | Client/server extern host portability, HTTP interoperability tests |
| Noqeri WebSocket | RFC 6455 bounded **native frame layer** with masking, length formats, control-frame and UTF-8 validation; close-code helpers fixed | Upgrade handshake, fragmentation reassembly, socket/TLS bridge, backpressure, live E2E tests |

## Quick verification

Native web build (no Node.js, browser JavaScript, npm, React, Vue, or Angular required):

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target noqeri-web noqeri-websocket-frame-test
ctest --test-dir build -R '^(noqeri-native-web|noqeri-websocket-frame)$' --output-on-failure
python3 tests/native_web_actions.py ./build/noqeri-web examples/native_web_actions
python3 tests/native_web_database.py ./build/noqeri-web
./build/noqeri-web drivers
./build/noqeri-web build examples/native_web_static dist/native
```

Browser view build and compiler checks (Node.js is only the **tool for this separate experimental browser compiler**, not required by the native mode):

```sh
node --test tests/web-framework.test.mjs tests/web-views.test.mjs
```

The new WebSocket codec is located at `Include/noqeri/websocket_frame.hpp` and has no socket, TLS, JavaScript or Node.js dependency. It deliberately rejects fragmented frames until complete, bounded message reassembly is designed; it does not implement upgrade negotiation. The Noqeri-authored `Lib/std/websocket.noqeri` remains an **extern host interface**; the tested C++ frame codec is not yet wired to that extern ABI and should not be advertised as an operational WebSocket server.

## Runtime safety

The native server accepts one copy of Host, Origin, Cookie, Content-Type and Content-Length, rejects unsupported Transfer-Encoding and control bytes, and checks the Host header against its **loopback listening port**. Native HTTP and database tests exercise valid and malformed requests. The app is still a development-only server bound to `127.0.0.1`; this does not make it safe for Internet exposure. Normal HTTP/1.1 request smuggling qualification across proxies and concurrent load tests remain separate release gates.

For native server actions, a mutation involving `db.execute` must consist of **one database call only**. Multiple independent operations or mixing database and session/`.nqkv` mutations are now rejected at startup. This avoids promising ACID semantics across two different stores. Database drivers and NoeSQL's actual engine remain in their own provider implementations. The `drivers` command reports compile-time provider availability; it does not claim a live PostgreSQL/MySQL connection has been verified.

## Packaging decision

`Lib/web/ui.noqeri`, the native renderer and WebSocket framing primitives are first-party runtime/standard-library foundations. They stay in **EMN90909/Noqeri** rather than publishing duplicates to **EMN90909/noqeri-registry**. Once the component API, host ABI, automated end-to-end tests and semver compatibility contract stabilize, publish separately versioned optional component kits and HTTP/WebSocket host adapters to the registry.

This qualification increment deliberately leaves the **EMN90909/NoeSQL** source unchanged.

## Release-specific qualification

The updated [Web production readiness and performance gates](WEB_PRODUCTION_READINESS.md) document now specifies which modes are Node-free, actual native/browser performance budget parameters and non-GA operations. The native web preview server stays **development-only** even when its workflow passes; no blanket production-ready claim is made for public deployments. Static hosting of native-generated HTML/CSS is a separate, bounded mode.
