# JetBrains / TextMate setup

JetBrains IDEs with TextMate bundle support can import `../vscode/syntaxes/noqeri.tmLanguage.json` and associate it with `*.noqeri`. Use the wolf/code asset from `../vscode/assets/noqeri-logo.svg` when building a dedicated plugin icon.

A generic LSP plugin can launch `noqeri lsp`. A dedicated JetBrains plugin should be added only when it implements richer behavior such as lifecycle management, navigation or diagnostics.
