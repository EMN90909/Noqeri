# Noqeri Web Framework (experimental)

[Framework qualification matrix](WEB_FRAMEWORK_QUALIFICATION.md) documents the UI/HTTP/WebSocket hardening, test evidence and outstanding production gates. The expanded UI library adds PageHeader, SectionHeading, Panel, EmptyState, FieldHint, FieldError, SecondaryLink, FooterNote, SubmitButton and TextAreaField to the original components.


For JavaScript-free **server-driven** actions, forms, session state and persisted data, see [WEB_NATIVE_ACTIONS.md](WEB_NATIVE_ACTIONS.md). For strictly static output use [WEB_NATIVE.md](WEB_NATIVE.md).


**Without Node.js:** use the native `noqeri web build` compiler for static HTML/CSS, or `noqeri web compile` / `noqeri web serve-app` for server-rendered forms and routes. The native action server can optionally enable browser-side hydration and client-side navigation with `NOQERI_WEB_REACTIVE=1`. None of those native build paths require Node/npm. The **separate** experimental `Runtime/web-project.mjs` client-view compiler still requires Node 22+ at build time; browsers execute its generated ES modules without Node.


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

**This remains an experimental view compiler, not Stage-2 typed NIR code generation.** Generated routes with deterministic local state, no third-party JavaScript, no persistent state, and no asynchronous data can now be statically pre-rendered into their HTML entrypoints. The browser adapter then hydrates matching DOM elements and attaches events without replacing them. Pages using browser-only async resources, persistence, external JS or mount effects remain CSR-only. This is static pre-rendering, **not** dynamic per-request SSR or universal server-side data loading. Route parameters, production-qualified hydration, mature accessibility primitives, source maps, and fully native compilation of the client-view grammar remain outstanding. Browsers necessarily consume HTML/CSS/JavaScript standards output; "Noqeri-only" means the application source and UI library, not replacing the browser engine.

The existing Noqeri native HTTP server library can provide backend endpoints, but the framework does **not** yet automatically compile an integrated server application from this view DSL.

## Native progressive enhancement (Node-free)

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target noqeri noqeri-web
./build/noqeri web build examples/native_web_static dist/site
NOQERI_WEB_REACTIVE=1 ./build/noqeri web serve-app examples/native_web_actions 8080
# Or compile a native executable (C++ bootstrap remains an implementation detail):
./build/noqeri web compile examples/native_web_actions dist/noqeri-app
```

The server creates the initial HTML. The opt-in client script attaches browser events without replacing the initial page and fetches only same-origin HTML to update the document after actions/routes. This is **progressive server rendering plus client-side navigation**, not arbitrary browser-only Noqeri execution, WASM or React-style independent client rendering. Disable JavaScript to retain fully functional server POST fallback. The preview native HTTP listener remains local-only and should not be exposed directly to the internet.
