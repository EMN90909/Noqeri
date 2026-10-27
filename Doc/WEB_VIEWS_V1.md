# Noqeri web views: a one-language UI direction (experimental)

The goal is one Noqeri language for the page's markup, reactive state, components, and browser behavior. The browser remains the runtime. There is no requirement to learn React or TypeScript to build the example. It is **not yet a Stage-2 typed NIR frontend** or a production-compatible replacement for browsers' HTML, CSS, and JavaScript standards.

```noqeri
let count = 0
view {
    h1 { "Hello" }
    p { "Count: " + count }
    button on.click { count = count + 1 } { "Click me" }
}
```

Run on Node 22+: `node Runtime/web-project.mjs examples/web_view /tmp/noqeri-web-view` or through the Stage-2 launcher: `noqeri web-project examples/web_view dist`. The builder accepts a root `main.noqeri` and generates `index.html`, standards-compliant ES modules and `__noqeri/runtime.mjs`. HTML and stylesheets remain optional interoperable assets. Browser rendering creates DOM elements and text nodes, never unsafe HTML strings.

**State:** Top-level `let` declarations are reactive when assigned from `on.click`, `on.input`, `on.change`, `on.submit` or `on.mount` blocks. The small DOM reconciler updates text and events while retaining nodes with unchanged element tags. Top-level `const` is immutable.

**Components:** `component Badge(label: string) { span { label } }` and `Badge("Ready")` work in views. A local UI library can expose `import ui/badge as ui` from `ui/badge.noqeri`, compiled to a standard `.mjs` import. Remote `.nqpkg` UI package installation and type-checked component contracts require further Registry/Stage-2 integration.

**Data:** `load status from "/api/status"` performs a bounded, same-origin JSON fetch and exposes `status.data`, `status.loading`, and `status.error`. The backend may later be written in Noqeri and served by a qualified host. It is **not** currently an automatically generated Noqeri server.

## JavaScript interop

```noqeri
import js "gsap" as gsap
view {
    div class: "card" { "Animate me" }
}
on.mount {
    gsap.to(".card", { x: 100, duration: 0.8 })
}
```

Bare module specifiers **must** resolve through an explicit `web.importmap.json` import map in the project root. Example mapping to a trusted, locally bundled ES module:

```json
{"imports":{"gsap":"./vendor/gsap.mjs"}}
```

You must provide that actual ES module (for example, build/vend a legitimate licensed GSAP ESM distribution). The builder does not fetch remote libraries, invent npm modules, inject CDN scripts, or silently fall back to an unverified global. `import js "./vendor/gsap.mjs" as gsap` works without an import map. The same approach applies to Three.js, D3, React Spring, and Monaco once their browser-compatible ESM packages are provided.

Local Noqeri wrapper functions are now implemented:
```noqeri
// animation/gsap.noqeri
import js "gsap" as gsap
function move(selector: string, distance: int) {
    return gsap.to(selector, { x: distance, duration: 0.8 })
}
```
An app can then write `import animation/gsap as animation` and `on.mount { animation.move(".card", 100) }`. The browser still needs a real, explicitly resolved `gsap` ES module in `web.importmap.json`. This is a local package interface, not yet a published Registry package. See `examples/web_view/animation/motion.noqeri` for a dependency-free working example. Method-chaining sugar such as `animate(".card").to(x: 100)` is **not yet a compiler feature**.

## Security and limitations

The view parser rejects arbitrary globals, raw HTML insertion, unrecognized tags, event attributes, unsafe URL schemes, missing imports, symlinks, and unknown expressions. An imported JavaScript package runs with the privileges of that browser origin, so only use reviewed dependencies. There is no SSR/hydration, CSS DSL, incremental typed NIR, compiler diagnostics parity, accessible component suite, production router, or automatic server-side code generation yet. This is a working experimental web frontend and runtime, not a production-qualified language capability.

The generated web output includes no React, Angular, or Vue dependency; `styles.css` is an optional conventional asset. `main.noqeri` remains the single source of behavior and markup.


## Persisted UI state, forms and WebSocket subscriptions (experimental)

Browser `persist` declarations store nonsecret primitive UI values in origin-local storage; `form.field` and `input.value` provide bounded event input and HTML-native `required`/length/range validation is available. `subscribe updates from "/ws/updates"` reconnects a same-origin WebSocket and exposes `updates.data`, `updates.connected`, and `updates.error`. An external WebSocket server is required: the native app preview does not handle WebSocket upgrades. See [cloud and realtime guide](WEB_CLOUD_REALTIME.md).
