# NQON 1.0 grammar

NQON (Noqeri Object Notation) is a language-neutral structured-data format. It is designed to be immediately recognizable to developers familiar with JSON while removing punctuation that beginners do not need.

A NQON document is an implicit root object:

```nqon
name = Aroeno
version = "1.0.0"
private = true

packages {
    forge = "noqeri/forge@1.2.0"
    postgres = "noqeri/postgres@2.0.0"
}
```

## Grammar

```text
document     := trivia entry* EOF
entry        := key trivia ("=" trivia value | object) separator?
key          := IDENT
value        := STRING | NUMBER | BOOLEAN | "null" | BARE | array | object
object       := "{" trivia entry* "}"
array        := "[" trivia (value separator? trivia)* "]"
separator    := ","
trivia       := (WHITESPACE | LINE_COMMENT)*
LINE_COMMENT := "//" characters-until-newline
IDENT        := (LETTER | "_") (LETTER | DIGIT | "_" | "-")*
BARE         := (LETTER | "_") (LETTER | DIGIT | "_" | "." | "/" | "-")*
```

Newlines and whitespace separate entries naturally. Commas are accepted for easy migration and compact arrays, but the canonical formatter omits them.

Strings use JSON-compatible double-quoted escaping. Bare values are strings unless they are exactly `true`, `false`, `null`, or a valid decimal number.

## Data model

NQON has the same core data model expected from a portable configuration format:

- object
- array
- string
- number
- boolean
- null

Duplicate object keys are invalid. Implementations should impose reasonable input-size and nesting limits at untrusted boundaries.

## Portability

NQON is not tied to the Noqeri compiler. Other languages can implement this grammar directly. The repository includes `Runtime/nqon.mjs` and `Runtime/nqon-cli.mjs` as a standalone JavaScript reference implementation and converter.
