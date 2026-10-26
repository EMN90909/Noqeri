# Optional browser WebAssembly in Noqeri — v0.1.4 investigation

**Default UI remains HTML-native and JavaScript-free**. An ordinary browser performs a full-page POST/redirect/GET cycle for `button on.click` and `form on.submit` with no JavaScript, WASM or DOM host runtime.

WebAssembly is **not itself a browser DOM API**. A bare `.wasm` module has linear memory, imported functions, compiled numeric operations and host calls. Browsers generally require a JavaScript or browser-provided integration path to instantiate WebAssembly and offer it DOM capabilities. Including a `.wasm` resource in a page does **not** make buttons reactive, hydrate HTML, expose events or provide direct native DOM access. A WASI module also does not automatically gain browser DOM privileges.

## Proposed independent enhancement profiles

| Profile | Interaction contract | JavaScript dependency |
| --- | --- | --- |
| Native default (implemented) | Server actions, real forms and links, server-rendered HTML/CSS, CSRF | None |
| Accessible static site (implemented) | Build to routes and CSS, browser-native links | None |
| Optional computation WASM | Compile pure Noqeri functions to sandboxed WASM, invoked through an explicit host API | Instantiation/host bridge usually requires an external browser integration |
| Optional rich client | Hydrate independent islands, listen to events, patch DOM, selective navigation | A small, audited JavaScript host adapter or future well-specified browser-native DOM imports is required |

For parity, every WASM-enhanced form must remain usable as an ordinary HTML POST and must pass the same server-side validation/authorization as the non-WASM path. Client computation is a performance/UX enhancement, **never an authorization or persistence boundary**.

## Release gate before adding a browser WASM target

1. Compile a real typed NIR fixture through a shared backend to WASM, not from the separate web bootstrap grammar.
2. Define a versioned, capability-limited host import interface with no ambient network, file system or DOM access.
3. Validate CSP policy, WASM memory, module size, CPU bounds and permission gating.
4. Qualify Chromium, Firefox and Safari behavior and accessibility with and without the host adapter.
5. Prove JS-free fallback and equal server-side form security in real browsers.

**v0.1.4 result:** feasibility and API boundary documented; **no operational WASM UI hydration runtime is claimed or published**.
