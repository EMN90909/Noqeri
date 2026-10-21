# noqeri 1.4 grammar

Noqeri deliberately keeps a familiar reading order while making low-level intent explicit. Semicolons are optional at statement boundaries, types follow names, casts use `as`, error propagation uses prefix `try`, and systems operations stay visible rather than being hidden behind compiler magic.

## Lexical layer

```text
IDENT          := (LETTER | "_") (LETTER | DIGIT | "_")*
DEC_INTEGER    := DIGIT+
HEX_INTEGER    := "0" ("x" | "X") HEX_DIGIT+
INTEGER        := DEC_INTEGER | HEX_INTEGER
FLOAT          := DIGIT+ "." DIGIT+
STRING         := '"' (ESCAPE | any-character-except-quote)* '"'
LINE_COMMENT   := "//" characters-until-newline
BLOCK_COMMENT  := "/*" characters-until-"*/" "*/"
```

Whitespace and comments may appear between tokens.

## Program structure

```text
program        := declaration* EOF

declaration    := modifiers? functionDecl
                | moduleDecl
                | importDecl
                | recordDecl
                | letDecl
                | statement

modifiers      := ("export" | "extern")+
moduleDecl     := "module" qualifiedName terminator?
importDecl     := "import" importTarget terminator?
importTarget   := STRING
                | "package" STRING
qualifiedName  := IDENT ("." IDENT)*
terminator     := ";"

functionDecl   := "function" IDENT genericParams?
                  "(" parameters? ")"
                  (":" type)?
                  (block | terminator?)

genericParams  := "<" IDENT ("," IDENT)* ">"
parameters     := parameter ("," parameter)*
parameter      := IDENT (":" type)?

recordDecl     := "record" IDENT "{" recordField* "}" terminator?
recordField    := IDENT ":" type ("," | terminator)?
```

There are deliberately two import forms and no third module-resolution syntax:

```nqr
import "./local_file.noqeri"
import package "noqeri/supabase"
```

A quoted import resolves relative to the importing source file. `import package` resolves an installed registry package by its `namespace/name` coordinate. Package coordinates may contain ASCII letters, digits, `_` and `-` in each segment and contain exactly one `/`. Versions do not appear in source code: `project.noqeri` declares the acceptable version and `noqeri.lock` pins the exact artifact and integrity identity. Compilation never silently changes a dependency version. Network access belongs to `noqeri add` / `noqeri install`; the compiler resolves imports from the immutable package cache and can therefore build offline.

This syntax exists to delete ambiguity between a local file and a registry dependency. It does not introduce aliases, URL imports, install-time scripts, or a second module system.

## Types

```text
type           := pointerType
                | sliceType
                | arrayType
                | namedType

pointerType    := "*" "volatile"? type
sliceType      := "[" "]" type
arrayType      := "[" type ";" INTEGER "]"
namedType      := IDENT
```

Stable integer names are:

```text
u8  u16  u32  u64
 i8  i16  i32  i64
usize  isize
```

The existing `int`, `float`, `bool`, `string`, `null`, and `void` types remain available.

Pointers are written `*T`. A volatile pointee access is written `*volatile T`. `&value` takes an address, `*pointer` dereferences, and indexing scales by the pointee or element size.

Fixed arrays use `[T; N]`; slices use `[]T`. A slice is a pointer-and-length view.

## Values and statements

```text
letDecl        := ("let" | "const") IDENT (":" type)? "=" expression terminator?

statement      := ifStmt
                | whileStmt
                | repeatStmt
                | returnStmt
                | throwStmt
                | block
                | exprStmt

ifStmt         := "if" expression block ("else" (ifStmt | block))?
whileStmt      := "while" expression block
repeatStmt     := "repeat" expression block
returnStmt     := "return" expression? terminator?
throwStmt      := "throw" expression terminator?
block          := "{" declaration* "}"
exprStmt       := expression terminator?
```

`const` bindings cannot be assigned after initialization. `if` and `while` conditions must be `bool`; integers are not silently treated as booleans.

`repeat count { ... }` is the beginner-friendly counted-loop form. The count must be an integer, is evaluated once before the loop begins, and values less than or equal to zero execute the body zero times. It lowers to the existing control-flow model rather than introducing a second loop runtime.

## Expressions

```text
expression     := assignment
assignment     := assignTarget "=" assignment
                | logicalOr

assignTarget   := IDENT
                | postfixTarget
                | "*" unary

postfixTarget  := primary (indexSuffix | memberSuffix)+

logicalOr      := logicalAnd ("||" logicalAnd)*
logicalAnd     := equality ("&&" equality)*
equality       := comparison (("==" | "!=") comparison)*
comparison     := term (("<" | "<=" | ">" | ">=") term)*
term           := factor (("+" | "-") factor)*
factor         := unary (("*" | "/" | "%") unary)*
unary          := ("!" | "-" | "+" | "*" | "&" | "try") unary
                | cast
cast           := postfix ("as" type)*
postfix        := primary (callSuffix | indexSuffix | memberSuffix)*
callSuffix     := "(" arguments? ")"
arguments      := expression ("," expression)*
indexSuffix    := "[" expression "]"
memberSuffix   := "." IDENT

primary        := INTEGER
                | FLOAT
                | STRING
                | "true"
                | "false"
                | "null"
                | IDENT
                | arrayLiteral
                | "(" expression ")"

arrayLiteral   := "[" (expression ("," expression)*)? "]"
```

Assignments are valid only to names, fields, indexed elements, or dereferenced pointers. Arbitrary expressions are not assignable.

## Integer conversion rule

Noqeri distinguishes **provably safe implicit conversion** from **explicit conversion**:

- same-type assignment is allowed;
- same-signedness widening is implicit, such as `u8 -> u32`;
- unsigned-to-signed widening is implicit only when the signed destination is strictly wider, such as `u32 -> i64`;
- narrowing is not implicit;
- signed-to-unsigned conversion is not implicit;
- an integer literal may initialize a fixed-width integer when its value is provably in range;
- otherwise use `as` to make the conversion explicit.

Examples:

```nqr
let port: u16 = 8080       // valid: literal fits
let small: u8 = 42
let wide: u32 = small      // valid: safe widening

let count: u64 = 300
let byte: u8 = count       // error: narrowing
let forced: u8 = count as u8
```

The explicit cast communicates that truncation or signedness reinterpretation is intentional.

## Arrays and slices

Array literals infer their element type from their elements. `slice(array)` or `slice(pointer, length)` creates a slice, and `len(value)` accepts arrays and slices.

```nqr
let values: [int; 4] = [1, 2, 3, 4]
let view: []int = slice(values)
print(len(view))
print(view[2])
```

## Modules and generics

`module name` gives a source file an organizational identity. Local `import "path.noqeri"` resolves recursively relative to the importing file. `import package "namespace/name"` resolves the package entry point from the exact artifact pinned in `noqeri.lock`. The package manager may download and verify an artifact, but the compiler itself never executes arbitrary package install scripts. Imported declarations currently enter one compilation graph; the package form changes resolution, not the language's symbol model.

Generic functions use inferred register-value type parameters:

```nqr
function identity<T>(value: T): T {
    return value
}
```

The bootstrap currently limits generics to values supported by the register-oriented ABI; aggregate monomorphization is not yet implied by this grammar.

## Error propagation

`throw code` and `try expression` provide allocation-free status propagation for integer-returning functions.

```nqr
function openDevice(): isize {
    throw 3
}

function start(): isize {
    let status: isize = try openDevice()
    return status
}
```

A thrown non-negative code is encoded as a negative status. `try` returns from the current function immediately when it receives a negative status.

## Systems built-ins

Atomics, CPU intrinsics, slicing helpers and inline assembly are ordinary call forms rather than separate grammar families:

```text
len(value)
slice(array)
slice(pointer, length)

atomic.load(pointer)
atomic.store(pointer, value)
atomic.exchange(pointer, value)
atomic.compareExchange(pointer, expected, desired)
atomic.fence()

intrinsic("x86.pause")
asm("nop")
```

Atomic operands must be bool, integer, or pointer values no wider than 8 bytes in the current backend.

`record` uses declaration-order, naturally aligned field layout. `extern function` declares a symbol supplied by the embedding/linking environment. `export function` emits the declared name as an externally visible native symbol.

These are general language facilities for applications, libraries, embedded software, runtimes, concurrency, FFI, kernels, and other performance-sensitive programs. They are not an operating-system-specific dialect.