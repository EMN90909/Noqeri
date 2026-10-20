# noqeri toolchain

The bootstrap pipeline is:

`source -> lexer -> parser/AST -> type checker -> NIR -> optimizer -> interpreter or native backend`

The executable is `noqeri`. `lex`, `check`, `nir`, `run`, `build`, `format`, `manifest`, `lock`, `test`, `doctor`, `release-check`, and `lsp` are the current commands.

CMake is the canonical bootstrap build. The language project format itself remains noqeri-native (`project.noqeri`), not CMake.
