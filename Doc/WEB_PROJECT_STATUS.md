# Web development status

## Working capability

The Stage-2 CLI provides `noqeri web-project <source-dir> <output-dir>`, a **static asset packager** for HTML, CSS, JavaScript modules, and common browser resources. It preserves directories, checks local HTML references for traversal, rejects symlinks and unsupported files, and prevents source/output overlap. It does not minify, bundle npm imports, or execute scripts. Run its regression tests locally with:

```sh
node --test tests/web-project.test.mjs
node Runtime/web-project.mjs examples/web_project /tmp/noqeri-web-output
# Or via a working Stage-2 launcher:
noqeri web-project examples/web_project /tmp/noqeri-web-output
```

The CLI command can package regular HTML/CSS/JS projects without loading the compiler module. The direct Node entrypoint likewise runs without a Stage-2 seed.

## Important limitations

- **Noqeri .nqr-to-JavaScript compilation is not implemented in the Stage-2 CLI.** The tool errors if a source tree contains .nqr; it does not silently copy uncompiled code or translate with string substitutions.
- The archived C++ `Compiler/web_backend.cpp` is a reference implementation and not integrated with Stage-2. The Stage-2 self-hosted compiler uses an NIR/NQO2 pipeline. Browser codegen should consume verified typed NIR and emit standards-compliant ES modules, with enforced restrictions on native pointers, assembly, arbitrary host capabilities and unsafe DOM sinks.
- The initial five static packaging regression tests were executed locally on a Node.js server and passed. **Compiler-to-browser smoke tests have not run successfully** because there is no seed compiler and the current runtime does not implement `web`.
- Browser execution tests should compile .nqr fixtures, import generated ES modules, verify exported functions and host-capability boundaries, check JSON and DOM integration, and test in at least Chromium and Firefox.
- The source import mechanism must explicitly specify interoperability rules for importing JavaScript modules and CSS assets; copying JS and CSS is not equivalent to integrating them with .nqr syntax.

No GitHub Actions are required or used.
