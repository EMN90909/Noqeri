# Noqeri beginner course

The beginner track deliberately teaches ordinary application programming before systems mechanisms. A learner should be able to build useful software without first understanding pointers, ABI details, atomics, assembly, native object formats or compiler IR.

## Core track

1. **Print** — run one file, print text and numbers, read an error message.
2. **Variables** — `let`, `const`, inferred everyday values, basic types and simple expressions.
3. **Decisions** — booleans, comparisons and `if`/`else`; parentheses around the condition are optional.
4. **Counted repetition** — `repeat 5 { ... }` when the learner simply means “do this five times”. No manual loop counter is required.
5. **Conditional loops** — `while condition { ... }` when the stopping condition is part of the problem.
6. **Functions** — parameters, return values and small reusable units.
7. **Records** — model a user/domain value and read/write fields.
8. **Collections** — lists/maps/sets/queues and real foundational containers through general-purpose APIs, not type-specific file trivia.
9. **Files** — read, transform and write a text file safely.
10. **JSON / CSV / encodings** — validate, parse and produce common interchange formats.
11. **HTTP** — make a request, then build a minimal endpoint.
12. **Database** — create a table, insert/update/select records and handle errors.
13. **Concurrency** — event/future/channel/task concepts first; race safety and cancellation before atomics.

Each chapter should end with one runnable program, one deliberate mistake exercise and one small extension. Course examples are compatibility-test inputs: a language change that breaks them requires either a migration or a conscious edition boundary.

## Project files stay optional at the beginning

A first Noqeri program does not need project metadata. When a learner grows into packages or project-wide settings, the default manifest is `project.nqon`:

```nqon
name = hello
version = "0.1.0"
entry = main.noqeri

packages {
    forge = "noqeri/forge@1.2.0"
}
```

NQON uses braces for structure rather than indentation, `=` for values, optional commas, and quoted strings only when needed. The exact dependency graph remains machine-managed in `noqeri.lock`.

## The two loop ideas

A beginner should not first learn a three-part loop header merely to count to five.

```nqr
repeat 5 {
    print("hello")
}
```

Use `repeat` when the number of iterations is the idea. The count is evaluated once. A zero or negative count executes zero iterations.

```nqr
let attempts = 3
while attempts > 0 {
    print(attempts)
    attempts = attempts - 1
}
```

Use `while` when the condition is the idea. `repeat` is lowered to the existing `while` semantics, so it does not introduce another runtime model.

## First collection examples

Teach the verb before storage mechanics. A learner should first see concepts such as:

```text
push -> get -> remove
set -> contains -> clear
send -> receive -> close
complete -> get result
```

Caller-owned buffers, capacity planning, pointer representations and monomorphisation can be explained after the learner understands what the abstraction does. The low-level controls stay available for freestanding and performance-sensitive software.

## Advanced track

Only after the core track introduce:

1. pointers and address lifetimes;
2. explicit `unsafe` blocks and why they are exceptional;
3. atomics and memory ordering;
4. C ABI/FFI ownership and error conventions;
5. native layouts and object formats;
6. inline assembly and target-specific code.

These are not removed or weakened. They are moved out of the first-learning dependency graph.

## Usability benchmarks

Beginner usability is measured, not asserted. Record anonymously aggregated task timing and blockers for:

- time to install + first successful `print`;
- time to diagnose the first syntax/type error;
- time to express a counted loop and a conditional loop;
- time to build a CLI;
- time to build a JSON transformer;
- time to build an HTTP API;
- time to build a small database CRUD application.

When comparing Python, Go, Lua and JavaScript, use equivalent tasks, fresh participants where possible, identical help constraints and a published study protocol. “Simpler than Go” is a product goal; comparative superiority should be demonstrated with usability evidence rather than declared from syntax alone.

## Language complexity budget

Every proposed language feature answers, in order:

1. Can this be a library feature? If yes, prefer the library.
2. Does an existing construct already express the idea clearly? If yes, do not add a synonym.
3. Does the feature remove a common beginner ceremony without introducing new semantics? If yes, small syntax sugar such as `repeat` can be justified by desugaring to the existing core.
4. Does it force ordinary users to learn pointers, atomics, ABI, native layout, assembly or another advanced mechanism? If yes, keep it opt-in.
5. Does it make diagnostics, formatting, tooling or migration materially harder? Include that cost in the proposal.

Safety ambition may be high while the language surface remains small. Predictable, boring syntax is a maturity feature.
