# Noqeri syntax simplicity contract

**Status:** 0.7 development design contract.

Noqeri targets a beginner-surface simplicity score of **9.7/10 or better** while remaining statically checked and systems-capable. That number is an internal design target, not an empirical ranking against Python or another language. Public comparative claims require the participant evidence defined in `Doc/USABILITY_STUDIES.md`.

## The beginner path

A first program is one line:

```noqeri
print("Hello, world!")
```

A beginner does not need a project manifest, module declaration, `main` function, semicolon or type annotation to run a simple source file.

Initialized locals infer their type:

```noqeri
let score = 10
const name = "Noqeri"
```

Conditions read directly:

```noqeri
if score >= 5 {
    print("high")
}
```

Conditional loops use one obvious spelling:

```noqeri
while score > 0 {
    score = score - 1
}
```

The counted-loop surface is:

```noqeri
repeat 5 {
    print("hello")
}
```

The bootstrap compiler supports this spelling. The Stage-2 parser, semantics and lowering now include a dedicated counted-loop path and focused regression tests. Those sources type-check in the bootstrap compiler. After fixing aggregate address lowering, six bounded parser/NIR tests execute under the Stage-0 interpreter and pass AddressSanitizer. Subsequent independent [GCC/Clang Stage-1 and Stage-2/3 NQRI byte-equality](https://github.com/EMN90909/Noqeri/actions/runs/40370068449) and [actual Stage-2 NQRI loop/function execution](https://github.com/EMN90909/Noqeri/actions/runs/40371256165) passed. **Native executable self-hosting remains unqualified.**

Functions stay explicit where types communicate an API boundary:

```noqeri
function add(a: int, b: int): int {
    return a + b
}
```

The Stage-0 compiler now infers a private function's return type for a single unconditional typed return (or expression body) when parameters are already typed. More complex flow, recursion, public APIs and generic ABI boundaries still need explicit annotations; parameter inference and self-host Stage-2 parity remain unverified. Omitting annotations must never silently turn a statically checked API into dynamic typing.

## Projects should not look like paperwork

`noqeri new demo` generates:

```nqon
name = demo
version = "0.1.0"
```

The conventional entry path, edition, application profile, default target and registry are compiler defaults. A user writes them only when changing them.

The generated source is:

```noqeri
print("Hello from Noqeri")
```

Package state belongs to `project.nqon` and `noqeri.lock`, not to source-code ceremony.

## Rules for new syntax

1. **One concept, one normal spelling.** Do not add aliases such as `fn` beside `function` or a second package-import keyword just to save characters.
2. **Infer when the compiler has proof.** Initialized local variables should not repeat obvious types.
3. **Make risk visible.** Narrowing casts, raw pointers, volatile access, atomics, FFI, intrinsics and assembly stay explicit.
4. **No indentation semantics.** Braces define structure; formatting cannot silently change program meaning.
5. **No required punctuation tax.** Semicolons stay optional at statement boundaries.
6. **Progressive disclosure.** A beginner can learn variables, decisions, loops, functions, records, files, HTTP and databases before pointers/ABI/atomics.
7. **Configuration is data.** NQON remains non-executable and portable; Noqeri source remains the programming language.
8. **Good diagnostics count as syntax usability.** An error should identify the problem and the next repair, rather than forcing a beginner to learn parser terminology.
9. **Compatibility is not permission for clutter.** Old spellings may remain readable during migrations without becoming recommended alternatives.
10. **A feature is not beginner-qualified until every active compiler stage agrees.** Grammar, parser, semantic analysis, lowering/backends and tests must match.

## The 9.7 target gate

The target is considered earned only when the usability study shows that beginners can complete the core tasks without hidden coaching:

- install and print a first value;
- create/use variables;
- diagnose a syntax and a type error;
- write a decision;
- write counted and conditional repetition;
- write/call a function;
- create a small CLI;
- read/write JSON or NQON data;
- perform a basic HTTP task;
- perform database CRUD;
- add a package without manually editing dependency internals.

Track completion rate, median/p90 time, help events and failure reasons. Compare languages under the same task protocol. Until that evidence exists, write **"9.7 target"**, not **"proven 9.7"**.

## North-star rule

> If the compiler can safely infer something, the beginner should not have to write it. If inference could hide dangerous behavior or make meaning unstable, keep it explicit.

This rule is intentionally stricter than "fewest characters wins." Noqeri is trying to remove ceremony, not information.

See [the verified feature-completion matrix](SYNTAX_IMPLEMENTATION_STATUS.md); a green Stage-2 build does not imply the unimplemented proposals are available.
