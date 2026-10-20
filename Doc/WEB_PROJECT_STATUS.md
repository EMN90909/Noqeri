# Web development status

## Current capability

The Stage-2 CLI provides two browser-oriented commands:

```sh
noqeri web input.nqr output.mjs
noqeri web-project web dist
```

`noqeri web` now requires the self-hosted Stage-2 compiler module to expose `stage2WebCheck`. Browser acceptance is owned by Noqeri source in `Compiler/selfhost/web_policy.nqr`: source is parsed, zero-initialized, semantically checked with unsafe mode disabled, lowered to NIR, checked against production instruction limits, and rejected when the lowered program requires atomics, inline assembly, CPU intrinsics, or explicitly unsafe NIR.

`noqeri web-project` compiles `.nqr` files instead of rejecting them. It preserves static web assets, rewrites HTML references such as `<script src="app.nqr">` to standards-compatible ES module references, compiles local Noqeri import graphs, resolves pinned package imports through the package resolver, rejects source/output overlap, rejects symlinks, validates HTML traversal, and prevents local `.nqr` imports from escaping the project root.

The portable JavaScript host in `Runtime/web-compiler.mjs` is an emission/packaging adapter. It is not the semantic authority: Stage-2 Noqeri validation must pass first when invoked through the CLI. Its current browser emission coverage includes functions, exports, extern host capabilities, typed parameters and locals, conditionals, loops, arrays, indexing, slices, casts, member access, assignments, local imports, and common scalar expressions.

## Verification

Focused host tests:

```sh
node --check Runtime/web-compiler.mjs
node --check Runtime/web-project.mjs
node --test tests/web-compiler.test.mjs tests/web-project.test.mjs
```

The October 20 regression set covers executable generated modules, arithmetic, annotations, control flow, arrays/slices/casts, host capability calls, dynamic/native-only rejection, local import graphs, static asset packaging, HTML rewriting, filesystem traversal, symlinks, nested output rejection, required HTML, and local Noqeri import containment.

Pull requests are additionally gated by `.github/workflows/compiler-web-safety.yml`, which builds the bootstrap compiler, checks the self-host compiler source, runs `tests/compiler_stage2_core.nqr`, and executes selected compiler regressions.

## Architecture and limits

The compiler frontend, semantic analysis, lowering, browser capability policy, and Stage-2 ABI are Noqeri-owned. Browser code is emitted as JavaScript because JavaScript modules are the deployment artifact for this target. The portable host emitter still walks source syntax to serialize JavaScript; it must not make independent type or capability decisions. Long-term architectural parity is to have emission consume compiler-owned AST/NIR data directly so there is no duplicated grammar surface in the host.

Local project imports are intentionally contained to the web project root. Package imports may resolve outside that root only through the package resolver and lock/pinning rules.

Browser host access remains explicit through `NoqeriHost`; arbitrary global-object access and dynamic execution are rejected by the portable emitter. Web output should still be tested in real browsers for DOM integration, CSP behavior, module MIME configuration, and engine compatibility before a production web-readiness claim.

## Example

```html
<div id="app"></div>
<script src="app.nqr"></script>
```

```nqr
extern function domText(selector: string, text: string): bool

export function render(): bool {
    return domText("#app", "Hello from Noqeri")
}
```

Building the project emits `app.js` and rewrites the HTML script to `type="module"`.
