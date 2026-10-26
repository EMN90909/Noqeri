# Noqeri Web Framework (experimental)

For JavaScript-free **server-driven** actions, forms, session state and persisted data, see [WEB_NATIVE_ACTIONS.md](WEB_NATIVE_ACTIONS.md). For strictly static output use [WEB_NATIVE.md](WEB_NATIVE.md).


**Without Node.js or JavaScript:** use the separate native static renderer documented in [WEB_NATIVE.md](WEB_NATIVE.md). It builds and serves HTML/CSS without a JS engine. Reactive browser events are not supported in that mode.


Noqeri now includes a **Noqeri-authored component library**, static-path client routing, and a design-token theme compiler. A web application can be authored entirely in `.noqeri`: no handwritten React, Vue, TypeScript, JavaScript, HTML, or CSS is required for these features.

```noqeri
import noqeri/ui as ui

theme {
    primary: "#304ac0"
    background: "#f7f8fc"
    radius: 12
}

let clicks = 0

route "/" {
    main class: "nq-page" {
        ui.Heading("Hello from Noqeri")
        a href: "/about" { "About" }
        button class: "nq-button" on.click { clicks = clicks + 1 } {
            "Clicks: " + clicks
        }
    }
}
route "/about" { ui.Card("About", "Written in Noqeri") }
view { h1 { "Not found" } }
```

Run with Node 22+ from the repository root:

```sh
node Runtime/web-project.mjs examples/native_web /tmp/native-noqeri-site
# Serve /tmp/native-noqeri-site with any ordinary static HTTP server.
node --test tests/web-framework.test.mjs tests/web-views.test.mjs
```

Generated files include `index.html`, one `<route>/index.html` for every non-root route (when the project supplies no custom HTML), `main.mjs`, `noqeri/ui.mjs`, `__noqeri/theme.css` and `__noqeri/runtime.mjs`. All page behavior, visual components, routes and theme tokens come from Noqeri source. The browser standards output is an implementation artifact, **not another language the app author needs to write**.

## Framework interfaces

`import noqeri/ui as ui` provides `Heading`, `Lead`, `Card`, `Badge`, `Notice`, `Button`, `LinkButton`, `NavLink`, `TextField`, and `Statistic`. Components are written in [Lib/web/ui.noqeri](../Lib/web/ui.noqeri). They are visual building blocks; to handle user interaction today, attach `on.click` directly to standard Noqeri view elements.

Routes use `route "/path" { ... }` with static, absolute, alphanumeric/hyphen/underscore segments only. Standard internal `a href: "/path"` links are handled with browser history; call `navigate("/path")` in event actions to navigate programmatically. Back/forward events update the current view. Unknown routes use the optional `view` fallback, or a default not-found heading. Direct deep links have separate generated HTML entrypoints on static hosts. Custom HTML projects need host-side rewrite/fallback or manually authored route entrypoints.

Theme keys: `primary`, `accent`, `background`, `surface`, `text`, `muted`, `border` (3- or 6-digit hex colors), and `radius` (integer 0–32 CSS pixels). Other CSS features can still be provided by an optional `styles.css` asset.

## Security and limitations

The generated runtime uses DOM text nodes, not `innerHTML`. Route input is constrained to relative-to-origin absolute paths, and unsafe CSS token values are rejected. Router event listeners are removed on disposal. Importing third-party JavaScript modules remains explicitly opt-in; the bundled UI framework itself has no npm dependencies.

**This remains the experimental web-view compiler, not Stage-2 typed NIR code generation.** Route parameters, SSR/hydration, server rendering, dynamic forms, mature accessibility primitives, source maps, bundling, per-route data loaders and qualified browser E2E testing are still future work. Browsers necessarily consume HTML/CSS/JavaScript standards output; "Noqeri-only" means the application source and UI library, not replacing the browser engine.

The existing Noqeri native HTTP server library can provide backend endpoints, but the framework does **not** yet automatically compile an integrated server application from this view DSL.
