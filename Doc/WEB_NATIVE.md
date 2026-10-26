# Noqeri Web — native, zero-JavaScript mode

The Noqeri web framework now has a **separate compiled native static mode**. A Noqeri source file describes pages, themes, and reusable UI components. The standalone `noqeri-web` program compiles them directly to static HTML/CSS; **neither building nor viewing these pages requires Node.js, npm, JavaScript, or a JavaScript engine**. The optional preview HTTP server is implemented natively, without Node.

## Compile and run

Build the native compiler once with an available C++17 toolchain and CMake:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target noqeri-web
ctest --test-dir build -R noqeri-native-web --output-on-failure

./build/noqeri-web build examples/native_web_static dist/native
./build/noqeri-web serve dist/native 8080
# Visit http://127.0.0.1:8080/
```

To serve production static assets, upload `dist/native` to a static hosting provider. No continuous Noqeri runtime process is required for static deployment. Native preview serving is **currently POSIX-only**, binds to loopback 127.0.0.1, and is not hardened as a public-facing production server.

For application authors:

```noqeri
import noqeri/ui as ui
const message = "No JavaScript"

theme { primary: "#304ac0" radius: 12 }

route "/" {
    main class: "nq-page" {
        ui.Heading(message)
        a href: "/about" { "About" }
    }
}
route "/about" { ui.Card("About", "Native static HTML + CSS") }
```

The compiler renders `index.html`, `about/index.html`, `theme.css` and optionally `404.html` if a fallback `view` is specified. Links are ordinary HTML links with native full-page navigation. It renders UI components directly from `Lib/web/ui.noqeri` rather than importing ES modules.

**Supported native subset:** static `route`, `view`, `theme`, `component`, `import noqeri/ui as ui`, constant `let`/`const`, HTML elements/attributes and static concatenation. Rendering compiles expressions from literals, known constants, and component arguments. The native compiler escapes HTML text and attributes; disallows script elements, event callbacks, foreign JS imports, unsafe URL schemes, and unknown symbols; checks fixed route paths and theme tokens. It fails closed rather than silently generating JavaScript.

**Limitations:** This mode intentionally has **no client-side reactive state, `on.click`, `load`, DOM manipulation, animation, or arbitrary code execution in the browser**. In particular, existing `examples/native_web` uses reactive events and must still use the earlier JavaScript-emitting web-view builder. For interactivity without JavaScript, the next major component is a native Noqeri HTTP application backend handling GET/POST forms, database interactions, authentication, and server-side rerendering; alternatively, browser-native WebAssembly with a DOM interface needs an explicit non-JS host design. Neither has been implemented by this static mode.

The native compiler is a **C++ bootstrap implementation**, using Noqeri view syntax and Noqeri-authored UI definitions, not a completely self-hosted language compiler. The web standards **HTML and CSS are still required for normal browsers**; web-page rendering cannot bypass those standards without replacing the browser. It is independent of **Node.js and JavaScript**, not independent of operating systems, browsers or HTML/CSS.

For projects requiring browser reactivity, see [WEB_FRAMEWORK.md](WEB_FRAMEWORK.md) and [WEB_VIEWS_V1.md](WEB_VIEWS_V1.md).
