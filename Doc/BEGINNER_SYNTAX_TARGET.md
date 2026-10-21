# Beginner syntax target

Noqeri has an internal **9.7/10 beginner-syntax design target**. This is a design rubric, not a claim from participant studies. Real usability claims must use the protocol in `Doc/USABILITY_STUDIES.md`.

The rubric scores the amount of syntax and project ceremony a beginner must understand for common work:

| Area | Target | Current design |
| --- | ---: | ---: |
| First program and output | 1.0 | 1.0 |
| Variables and inference | 1.0 | 1.0 |
| Conditions | 1.0 | 1.0 |
| Counted repetition | 1.0 | 1.0 |
| Conditional loops | 1.0 | 1.0 |
| Functions | 1.0 | 0.8 |
| Collections and indexing | 1.0 | 1.0 |
| Imports/packages | 1.0 | 0.9 |
| Project configuration | 1.0 | 1.0 |
| Punctuation and consistency | 1.0 | 1.0 |
| **Total** | **10.0** | **9.7** |

## Why the score is not 10

Functions intentionally keep explicit type information where the compiler cannot yet infer it safely. Package imports also remain explicit rather than adding a second ambiguous shorthand.

Those two small costs preserve static guarantees and keep the language grammar predictable.

## Beginner surface

```noqeri
let name = "Ada"

if name == "Ada" {
    print("hello")
}

repeat 3 {
    print(name)
}
```

Project configuration uses NQON:

```nqon
name = hello
version = "0.1.0"

packages {
    forge = "noqeri/forge@1.2.0"
}
```

The score must not be increased by adding aliases or hiding unsafe behavior. A change earns simplicity only when it removes a concept, removes required ceremony, or makes a common task more predictable.

## Evidence gate

The 9.7 score above describes the syntax design against this repository-owned rubric. It does **not** establish that beginners learn Noqeri faster than Python, JavaScript, Lua, Go, or any other language. Comparative claims require participant results from the existing usability study tooling.
