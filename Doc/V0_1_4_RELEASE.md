# Noqeri v0.1.4 — public preview qualification

**Status:** experimental preview; **not** a production-ready full-stack, TLS, database-isolated or fully self-hosted framework. The GitHub Release with exact tag `v0.1.4` must be marked **prerelease**, not latest stable, until the release gates listed below pass. Release jobs require executable test results, not a manifest-only version change.

## Implemented in this increment

- Compiler/toolchain version aligned to `0.1.4` in `CMakeLists.txt`, `Include/noqeri/version.hpp`, `project.noqeri` and `project.nqon`.
- `noqeri-web compile <source-dir> <executable>` packages one `main.noqeri` app and its UI library source into a self-contained POSIX **native C++17 bootstrap executable**. It still parses restricted web syntax on startup; it is **not** code generation from the main typed NIR.
- Shared `layout { ... slot {} ... }` and `route "/members/:member"` render with exact-route priority and fail-closed ambiguous route detection. Static `build` remains limited to static paths.
- HTML-native `form on.submit` with `validate(field, minBytes, maxBytes)`, server validation before side effects, session state, CSRF, duplicate-header checks, loopback Host checks and input/output limits.
- Native app HTTP accepts at most 32 concurrently ingested requests and **serializes** state/database mutation and rendering under a mutex. This is bounded concurrent I/O, **not independently parallel transactions** or proven high-load production readiness.
- Optional browser-native Basic authentication using `NOQERI_WEB_BASIC_USER` and `NOQERI_WEB_BASIC_PASSWORD`. Set `NOQERI_WEB_SECURE_COOKIE=1` only when the public-facing reverse proxy terminates HTTPS. Server binds to localhost; **do not publicly expose this development server without a hardened proxy, TLS and independent security review**.
- Native SQLite adapter returns typed JSON scalar cells (string, number, null); additional engines continue to use their native client interfaces. There is no automatic NoeSQL-to-SQL conversion and no database engine change in NoeSQL itself.
- GPL-3.0-only compiler/toolchain and Apache-2.0 project-maintained reusable libraries are documented under `LICENSES.md`; exact source provenance and third-party notices take precedence. Both license texts ship in SDK archives.

## Compile a native application

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target noqeri-web
./build/noqeri-web compile examples/native_web_fullstack /tmp/my-noqeri-app
/tmp/my-noqeri-app 8080 /tmp/noqeri-session.nqkv
# HTTP (loopback only): http://127.0.0.1:8080/members/Ada
```

Bundling currently requires a local C++17 compiler and the GPL bootstrap source shipped with the Noqeri SDK. The resulting **executable itself** does not load `main.noqeri`, UI library source, Node or browser JavaScript. Optional database SDKs are not automatically linked into this AOT bundler; the compiled app supports the NoeSQL native executable provider via `NOESQL_BIN` but external SQL adapters need an explicit native library link/build path. Full direct typed Noqeri binary code generation remains a future milestone.

## Verification before any public Release

- Actual C++17 compiler and NQRI binaries compile and `noqeri --version` identifies `0.1.4`.
- CTest native web, WebSocket frames, NQRI smoke tests and independently launched `noqeri-web compile` executable run successfully.
- Python HTTP tests cover dynamic routes, templates, form validation, CSRF, authorization, concurrent ingestion, strict cookies and native SQLite query values.
- Assets include GPL and Apache texts and SHA256SUMS; download is publicly visible in `https://github.com/EMN90909/Noqeri/releases/tag/v0.1.4`. **A workflow or an open PR is not proof a public Release exists; verify the release API and artifacts.**

## Major capabilities still *not* delivered

| Goal | Remaining implementation and tests |
| --- | --- |
| Unified full-stack | Typed loaders with row iteration; real schema/typed router IR; flexible nested layouts; streaming and error boundaries |
| Production HTTP/auth | Authz/RBAC, audited identity lifecycle/password storage, TLS/proxy proof, structured tracing with request IDs, durable/shared sessions, resilience/load/fuzz testing |
| First-class NoeSQL | Native NoeSQL v2 session provider linked into Noqeri runtime; typed result/transactions/migrations; real Noqeri→Rust/Zig WAL plus live SQLite/Postgres/DuckDB/MySQL interoperability |
| Smoother JS-free UX | Optional WebAssembly component + browser host API feasibility and accessibility fallback; WebAssembly alone cannot inherently manipulate browser DOM without supported host bindings |
| Typed NIR | Shared compiler-owned web AST and type checking, NIR lowering, pure deterministic backend, Stage-2/Stage-3 bootstrapping and parity tests; current C++ bootstrap parser is still separate |
| Cross-platform stable release | Windows/macOS/BSD, Android, real native databases, TLS and browser E2E release matrices all qualified and passing |

## Release policy

`v0.1.4` denotes a **public developer preview**; no “production-grade” marketing claim. The automated release pipeline publishes only when its own Linux/x86_64 build, smoke and packaging checks pass. It includes a Linux/x86_64 SDK with compiler, VM, native web tool, library sources, license terms and checksums. Other architectures require independent qualification and may arrive through the existing platform-preview workflow. The canonical Release and tag should never be overwritten with a different commit.
