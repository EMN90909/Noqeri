## October 21: experimental native Noqeri view project

`Runtime/web-view-compiler.mjs` now parses `let` state, `view` nodes, events, Noqeri components, local UI package imports, `load ... from` JSON resources, mount effects, exported Noqeri wrapper functions and explicit `import js` ES-module interop. `Runtime/noqeri-web-runtime.mjs` provides safe text-based DOM reconciliation and event scheduling with no React requirement. `Runtime/web-view-project.mjs` generates `index.html` from `main.noqeri`, an `__noqeri/runtime.mjs` module, optional `styles.css`, and `web.importmap.json` when bare JS imports are used.

The feature is qualified by [CI workflow](../.github/workflows/web-views.yml) for the bounded JS frontend and browser-emulation tests, **not** by the self-hosted Stage-2 typed/NIR compiler. No claim of SSR, hydration, secure arbitrary JavaScript packages or production-grade browser parity is made. See [syntax and packages](WEB_VIEWS_V1.md) and `examples/web_view/`.

---
# Web development status

## October 20 update: Noqeri web project integration

`noqeri web-project <source-dir> <output-dir>` now compiles supported `.noqeri` files to `.mjs` ES modules using the existing restricted browser backend, and rewrites relative HTML `src`/`href` references from `.noqeri` to `.mjs`. Unsupported browser constructs fail closed, and output collisions such as `app.noqeri` alongside `app.mjs` are rejected. Use `<script type="module" src="app.noqeri"></script>` in source HTML. The output HTML references `app.mjs`.

This is a practical integration fix, **not** yet the full Noqeri compiler backend. The restricted JavaScript parser remains separate from the typed NIR compiler and does not support all Noqeri syntax. The web-project builder does not independently run Stage-2 semantic checking when invoked directly with Node. CI/CLI qualification must verify Stage-2 checking, runtime correctness and real-browser compatibility before claiming full support.

Run `node --test tests/web-project.test.mjs tests/web-compiler.test.mjs` with Node 20+ to exercise the regression suite. The current repository edits have not been executed on a checkout from this session because network access to GitHub from the test container is unavailable.

## Working capability

The Stage-2 CLI provides `noqeri web-project <source-dir> <output-dir>`, a **static asset packager with restricted `.noqeri` browser module generation** for HTML, CSS, JavaScript modules, and common browser resources. It preserves directories, checks local HTML references for traversal, rejects symlinks and unsupported files, and prevents source/output overlap. It does not minify, bundle npm imports, or execute scripts. Run its regression tests locally with:

```sh
node --test tests/web-project.test.mjs
node Runtime/web-project.mjs examples/web_project /tmp/noqeri-web-output
# Or via a working Stage-2 launcher:
noqeri web-project examples/web_project /tmp/noqeri-web-output
```

The CLI command can package regular HTML/CSS/JS projects without loading the compiler module. The direct Node entrypoint likewise runs without a Stage-2 seed.

## Important limitations

- **Complete Noqeri .noqeri-to-JavaScript compilation is not implemented.** The web-project builder supports the subset accepted by `Runtime/web-compiler.mjs`; this is not typed NIR code generation.
- The archived C++ `Compiler/web_backend.cpp` is a reference implementation and not integrated with Stage-2. The Stage-2 self-hosted compiler uses an NIR/NQRI pipeline. Browser codegen should consume verified typed NIR and emit standards-compliant ES modules, with enforced restrictions on native pointers, assembly, arbitrary host capabilities and unsafe DOM sinks.
- The initial five static packaging regression tests were executed locally on a Node.js server and passed. **Compiler-to-browser smoke tests have not run successfully** because there is no seed compiler and the current runtime does not implement `web`.
- Browser execution tests should compile .noqeri fixtures, import generated ES modules, verify exported functions and host-capability boundaries, check JSON and DOM integration, and test in at least Chromium and Firefox.
- The source import mechanism must explicitly specify interoperability rules for importing JavaScript modules and CSS assets; copying JS and CSS is not equivalent to integrating them with .noqeri syntax.

No GitHub Actions are required or used.

## October 17 web compiler progress

The Stage-2 CLI now exposes `noqeri web input.noqeri output.mjs`, which first invokes Stage-2 semantic checking and then emits ES modules using `Runtime/web-compiler.mjs`. This is **an intentionally restricted implementation**, not full language parity: functions, typed parameters, exports, returns, basic expressions, scalar local declarations, if/while, and explicit extern host calls are its initial supported syntax. Unsupported imports, arrays, property access, raw native constructs, complex types and advanced constructs are rejected, not silently miscompiled.

Browser interaction is explicit: `Runtime/browser-host.mjs` installs limited safe text/class DOM operations and caller-supplied JavaScript functions through `NoqeriHost`. HTML, CSS and JS are still ordinary web assets via `web-project`. For example:

```sh
noqeri web tests/web_smoke/01_exports.noqeri dist/app.mjs
noqeri web-project examples/web_project dist-static
node --test tests/web-compiler.test.mjs
```

**Verification scope:** The initial restricted compiler prototype was exercised on a local Node.js server with eight focused smoke checks passing after one test expectation correction. The checked-in implementation was reviewed through the GitHub connector but was not executed from a full repository checkout. The normal Stage-2 compiler CLI and its browser integration have not been exercised end-to-end here: the checkout failed due DNS and a trusted compiler seed is unavailable. Do not claim production maturity, type-system completeness, browser compatibility or security certification from this test coverage. To reach mature status, replace the restricted source parser with code generation from typed compiler NIR, bootstrap and run the full language suite, test browser imports, and add end-to-end Chromium/Firefox coverage.
