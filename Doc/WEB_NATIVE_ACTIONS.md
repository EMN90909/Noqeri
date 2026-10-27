# Native Noqeri server actions (experimental)

`noqeri-web serve-app` renders Noqeri pages directly on a native HTTP server. Buttons and forms update state using **ordinary HTML POSTs and full-page rerendering**. No Node.js, client-side JavaScript, npm, WASM or additional backend language is used to execute these actions.

```sh
cmake -S . -B build
cmake --build build --target noqeri-web
./build/noqeri-web serve-app examples/native_web_actions 8080 noqeri-data/demo.nqkv
# Browse http://127.0.0.1:8080/
```

```noqeri
let count = 0                 // per-session state, forgotten on restart
persist total = 0            // shared native durable key/value state

route "/" {
    main {
        p { "Session clicks: " + count }
        p { "Total clicks: " + total }
        button on.click {
            count = count + 1
            total = total + 1
        } { "Click" }
        form on.submit {
            label = form.displayName
        } {
            input type: "text" name: "displayName" {}
            button type: "submit" { "Save" }
        }
    }
}
```

For a complete working sample, see [examples/native_web_actions/main.noqeri](../examples/native_web_actions/main.noqeri). In the snippet above, also declare `let label = ""` for the form action.

## Supported and unsupported

* **Buttons:** `button on.click { state = state + 1 }` submits a framework-generated POST. The page updates after server processing and navigation; it is not DOM reactivity or AJAX.
* **Forms:** `form on.submit { state = form.fieldName }` reads a declared input name and saves an escaped value. Only `application/x-www-form-urlencoded`, not files, is supported.
* **State:** top-level `let` is isolated by a random session cookie; top-level `persist` is shared across sessions and stored in the explicit local `.nqkv` file. `const` remains immutable. The supported action expression forms are `variable = literal`, `variable = anotherVariable`, `variable = variable + positiveInteger`, and `variable = form.fieldName`. Integer addition is checked. Limited string concatenation is also accepted.
* **Persistence:** the native `NQKV1` format is a small file-backed key/value snapshot, with atomic rename in one server process. It is **not NoeSQL**, a relational database, a multi-process transactional store or an encrypted database. Crash durability, schema evolution, concurrency and database integration need further development.
* **Security:** route-scoped action IDs, per-session random CSRF tokens rotated after successful POST, same-site HTTP-only cookies, optional same-origin checks, bounded input, no dynamic code execution, and escaped HTML.
* **Operations:** the native server is loopback-only on POSIX, **for development/testing, not Internet exposure**. It has no TLS, accounts/authz, DoS protection, professional HTTP parser, long-lived session persistence or production deployment certification. Session state expires after inactivity/restart. Sessions are capped at 256.
* **HTML/CSS and optional hydration:** with JavaScript disabled or with `NOQERI_WEB_REACTIVE` unset, actions are accessible normal HTML POSTs with full-page navigation. Set `NOQERI_WEB_REACTIVE=1` when starting the native server to serve a small first-party browser script: it hydrates the existing server HTML through delegated events, performs same-origin form POSTs and GET link navigation, incrementally reconciles DOM nodes, and honors browser back/forward. CSP allows same-origin `fetch` only in this mode. Server-side validation, session cookies, CSRF, and the native render remain authoritative. On a failed reactive POST the client reloads the target page with GET rather than resubmitting a potentially committed action. The static `noqeri-web build` mode still ships no JavaScript.

**Next integration milestone:** bind the new `persist` abstraction to the NoeSQL native storage layer with explicit transaction semantics and add native Noqeri action compilation via Stage-2 typed NIR, rather than extending the current C++ bootstrap DSL parser.

To try native browser enhancement locally:

```sh
NOQERI_WEB_REACTIVE=1 ./build/noqeri-web serve-app examples/native_web_actions 8080
```

It uses a browser-native ES script shipped by the C++ bootstrap. Node.js and npm are not used to compile or serve the application. This mode is not a fully independent CSR compiler: HTML still comes from the Noqeri native server.
