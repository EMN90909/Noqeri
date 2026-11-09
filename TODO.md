# Noqeri TODO: API, Memory Safety, and Native Performance

> **Next work session:** Wednesday, **2026-11-11** (Africa/Nairobi).
> **Status:** Open items identified from the code review of [`9f6ba8a`](https://github.com/EMN90909/Noqeri/commit/9f6ba8a6e2ebe472b57e890344a7dc7838099979) and the benchmark changes in [`3ff0470`](https://github.com/EMN90909/Noqeri/commit/3ff04705223af49d1217e959611a47df4970b428).
> These are **work items to implement or verify**, not claims that every listed security issue is exploitable. Keep Noqeri's beginner-friendly source syntax intact.

## Tomorrow's starting checklist (2026-11-11)

- [ ] Run a fresh local build and all relevant tests. Record commands, platform, compiler versions and results; do not report tests as passed without executing them.
- [ ] Start with **P0: method-aware API routing**, **P0: response JSON typing**, and **P0: request-validation/security regression coverage**.
- [ ] For each change, run the interpreter, LLVM-powered and direct-native regression suites when applicable.
- [ ] Review outstanding items here, tick only completed and verified work, then commit source and test changes.

## P0 | API correctness and security before public use

- [ ] **Method-aware REST routes:** Associate endpoints with explicit HTTP methods (GET, POST, PUT, PATCH, DELETE, HEAD) rather than path alone. Respond with 405 plus an accurate `Allow` header for an existing path with an unsupported method. Test that POST/DELETE cannot accidentally execute a GET JSON route.
  - Current reference: `Compiler/bootstrap/web_native_app.hpp` matches JSON routes by path; `Compiler/bootstrap/web_native.cpp` accepts `api` blocks.
- [ ] **Typed JSON values:** Eliminate guessing JSON types by testing whether the rendered value contains only digits or equals `true`/`false`. Support strings, numbers, booleans, null, escaping and nested objects/arrays with explicit type-aware serialization. Ensure a string like `"1042"` remains a string and large numeric values remain exact.
- [ ] **Validate incoming API data:** Parse JSON request bodies with size/depth limits, validate per-route parameters and typed payloads, and return consistent 400/415/422 responses. Define how query parameters and path parameters are decoded/validated.
- [ ] **Authorize each API endpoint:** Make authentication and per-route authorization intentional and testable. Verify default behavior for public and private routes, denied methods and objects belonging to another user. Ensure secrets never appear in errors or logs.
- [ ] **Harden HTTP protocol behavior:** Test method handling, Content-Length framing, malformed requests, duplicated/conflicting headers, request deadlines, timeouts and incomplete bodies. Confirm 413 is returned for oversized bodies; do not treat a dropped connection as a passing 413 test.
- [ ] **Review concurrency and resource limits:** Exercise the 32-client cap, request deadlines, rate limiting and error paths under load. Confirm safe thread cleanup and responses when the server is at capacity. Validate behavior behind a trusted reverse proxy.
- [ ] **TLS and deployment guidance:** Document HTTPS termination, loopback binding, trusted proxy assumptions, `NOQERI_WEB_PUBLIC_ORIGIN`, cookie security, secret management and safe production deployment. Do not claim the current loopback HTTP server directly provides TLS.

## P0 | Memory safety across all backends

- [ ] **Expand bounds tests:** Cover negative indices, index equal to length, very large indices, empty arrays, nested arrays/slices, read and write paths, and dynamic index arithmetic for the interpreter, LLVM-powered backend, direct native backend and portable-C/GCC mode where supported.
- [ ] **Check diagnostic and exit parity:** Assert `NQR-R4000`, nonzero exit and no unexpected signal; document whether index and length appear in each backend. Test behavior when the ABI output callback fails.
- [ ] **Audit pointer lifetime and arithmetic:** Test out-of-range pointer offsets, signed multiplication overflow, alignment, slicing and pointer casts; review dangling pointer and use-after-free risks. Bounds trapping alone does **not** establish complete memory safety.
- [ ] **Add memory safety fuzzing/sanitizers:** Add randomized differential tests, ASan/UBSan builds where possible and minimized regression fixtures. Ensure compiler flags like `-fwrapv` are not presented as universal memory-safety guarantees.

## P1 | Finish the native API developer experience

- [ ] **Make the API language ergonomic:** Keep concise `api`/`json` blocks, but design a minimal explicit way to specify methods, request payloads, status codes and errors; document syntax compatibility.
- [ ] **Document current compilation architecture accurately:** `noqeri-web compile` currently embeds Noqeri source into a C++ web-engine executable (`Compiler/bootstrap/web_native_bundle.hpp`), rather than lowering HTTP handlers directly through typed Noqeri NIR. Describe this honestly in docs.
- [ ] **Compile API handlers through Noqeri's optimized pipeline:** Design, implement and benchmark NIR-backed native HTTP handlers while retaining a working bootstrap path. Check identical API behavior across builds.
- [ ] **Add HTTP client and server library coverage:** Provide tested standard-library/networking interfaces, JSON, robust errors, TLS integration via established libraries and database-query helpers without forcing verbose beginner syntax.
- [ ] **Broaden E2E API tests:** Add all HTTP verbs, 404/405/413, malicious input, invalid JSON, escaping, authentication and authorization, security headers, concurrency, timeouts and compiled-binary parity.
- [ ] **Performance and footprint tests:** Benchmark native APIs against equivalent Go, Rust and Node.js servers with matching routes, JSON payloads and concurrency. Report requests/sec, p95/p99 latency, CPU, memory, binary size and startup time.

## P1 | Benchmark fairness and compiler improvements

- [ ] **Native CPU parity:** Add `-mcpu native` to the Zig benchmark build when supported by the installed Zig version, and record target flags for every language.
- [ ] **C/Clang comparison:** Build equivalent C and C++ baselines with Clang as well as GCC to isolate the effect of Clang's optimizer on Noqeri's LLVM-powered backend.
- [ ] **Improve timing quality:** Increase runs, use batched iterations for sub-millisecond workloads, alternate or randomize execution order, publish spread/uncertainty and check the checksum on **every** sample.
- [ ] **Full-output equivalence:** Test randomized matrices and arrays, not only a single aggregate checksum; check algorithm, numeric representation, initialization and allocation-location parity.
- [ ] **Optimize direct x86-64 backend:** Inspect assembly against the Clang-generated output, prioritizing matrix loops, vectorization, bounds-check elimination where proven safe, register allocation and redundant loads/stores. Do not trade correctness for benchmark scores.
- [ ] **Regression gates:** Add optimizer equivalence, API security and direct-native performance regression tests without relying on unverified performance claims.

## P2 | Documentation, language guarantees and release readiness

- [ ] **Define safety guarantees:** Specify what Noqeri guarantees for bounds, integer overflow, ownership/lifetimes, data races, unsafe FFI and compiler optimization modes. Keep promises narrower than the implemented checks.
- [ ] **Write security/deployment docs:** Provide a local tutorial and an explicitly hardened reverse-proxy production deployment guide.
- [ ] **Release compatibility:** Keep existing beginner-friendly syntax working; add backward-compatibility tests before adding new grammar.
- [ ] **Track evidence:** For each completed item, link its commit, tests and benchmark output. A green checklist requires reproducible verification.

## Reference files

- `Compiler/bootstrap/web_native.cpp`
- `Compiler/bootstrap/web_native_app.hpp`
- `Compiler/bootstrap/web_native_bundle.hpp`
- `Compiler/native_backend.cpp`
- `Compiler/portable_c_backend.cpp`
- `tests/native_web_rest_api.py`
- `tests/bounds_safety.py`
- `benchmarks/suite/rebuild_and_benchmark.sh`
- `benchmarks/suite/rebuild_and_benchmark.py`

**Reminder note:** Resume this list on **Wednesday, November 11, 2026**. A date in this file is a planning note, not a scheduled notification.
