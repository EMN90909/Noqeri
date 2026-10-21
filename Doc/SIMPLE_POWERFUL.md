# Noqeri: simple enough to guess, powerful enough to grow

Noqeri's design target is not "more syntax than Go". It is **less ceremony per useful result** while keeping systems behavior explicit when it matters.

## The simplicity contract

1. One source language: `.noqeri`.
2. One local import form: `import "./file.noqeri"`.
3. One package import form: `import package "noqeri/forge"`.
4. Project metadata and acceptable package versions live in portable `project.nqon`; exact versions and SHA-256 identities live in `noqeri.lock`.
5. Common work should have a short path. Advanced work should not require a different language.
6. No hidden C/C++ dependency in normal compiler source.
7. Host capabilities such as files, sockets, TLS and clocks are explicit `extern` boundaries.
8. Allocation, pointers, atomics, volatile access and inline assembly stay visible rather than becoming invisible runtime magic.

## Zero-ceremony start

A single file can run without a manifest:

```noqeri
print("hello from Noqeri")
```

A generated project keeps defaults implicit:

```nqon
name = demo
version = "0.1.0"
```

The compiler already knows the conventional entry path, edition, app profile, default target and registry. Beginners should not copy configuration they did not choose.

## Lazy-friendly defaults

```nqr
import package "noqeri/forge"

function main(): int {
    get("/", "hello from Noqeri")
    serve(8080)
    return 0
}
```

Add a package once with `noqeri add noqeri/forge`; `noqeri install` is deterministic and `noqeri check app.noqeri` resolves the locked package.

## Advanced without becoming noisy

The same language supports fixed-width integers, records, arrays and slices, raw pointers, explicit casts, `try`/`throw`, atomics, volatile memory, imports, generics, `extern`/`export`, intrinsics and constrained inline assembly. This is progressive disclosure: simple programs stay simple while systems code is not forced into another language.

## Design benchmark

Go is a useful benchmark because it keeps the language small and makes concurrency/networking practical. Noqeri should compete by keeping the surface approachable while offering explicit low-level control, deterministic packages and a Noqeri-owned bootstrap. This is a design objective, not a claim that Noqeri already outperforms Go in production benchmarks.

## Simplicity measurement

The internal beginner-syntax target is 9.7/10 or better, but that number is a design target, not a published empirical result. Comparative claims require the repository's usability-study protocol and participant evidence. See `Doc/SYNTAX_SIMPLICITY.md` and `Doc/USABILITY_STUDIES.md`.
