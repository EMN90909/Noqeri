# Noqeri Web — native, zero-JavaScript mode

**New: interactive native applications.** [WEB_NATIVE_ACTIONS.md](WEB_NATIVE_ACTIONS.md) documents the standalone `noqeri-web serve-app` path for JavaScript-free server-rendered buttons, forms, session state and durable key/value data. The static `build` path described below remains unchanged.


The Noqeri web framework now has a **separate compiled native static mode**. A Noqeri source file describes pages, themes, and reusable UI components. The standalone `noqeri-web` program compiles them directly to static HTML/CSS; **neither building nor viewing these pages requires Node.js, npm, JavaScript, or a JavaScript engine**. The optional preview HTTP server is implemented natively, without Node.

## One-file websites (frontend + backend + CSS)

A native Noqeri web app can now live in **one** `.noqeri` file. The
`route` body defines the HTML frontend, `form on.submit` contains
server-side actions, and a multiline `style """ ... """` declaration
provides CSS. The existing `theme`, `database` and `query`
declarations can live alongside them. No separate HTML, CSS, Node or
JavaScript source files are required for the server-rendered path.

```noqeri
let greeting = "Hello!"
style """
.hero { max-width: 40rem; margin: auto; padding: 2rem; }
@media (max-width: 600px) { .hero { padding: 1rem; } }
"""

route "/" {
    main class: "hero" {
        h1 { greeting }
        form on.submit {
            validate(message, 2, 64)
            greeting = form.message
        } {
            input name: "message" {}
            button type: "submit" { "Save" }
        }
    }
}
```

```sh
# Run the app from its source file, including server-side form handling:
./build/noqeri-web serve-app examples/native_web_single_file.noqeri 8080

# Or compile it into a self-contained native server binary (POSIX):
./build/noqeri-web compile examples/native_web_single_file.noqeri ./my-web-app
./my-web-app 8080

# For pages without server actions, output static HTML/CSS:
./build/noqeri-web build examples/native_web_single_file_static.noqeri dist/one-file
```

The former directory input (`project/main.noqeri`) still works. Custom
CSS is appended after Noqeri's theme stylesheet, so selectors can override
theme defaults. The raw multiline style is limited to 64 KiB, written as
a **same-origin external CSS asset**, and subject to the existing Content
Security Policy. This is a web-framework syntax feature, not a change to
the general-purpose Noqeri language grammar. Server actions are currently
restricted to the supported native form/mutation subset; this is **not**
an arbitrary server-function runtime.

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
