# noqeri for VS Code

This extension package registers `.noqeri` files as noqeri, provides TextMate highlighting, comments/brackets, snippets and a noqeri file-icon theme.

The extension icon and `.noqeri` file icon use `assets/noqeri-mark.png`, derived from the canonical uploaded wolf/code artwork in `Brand/noqeri-logo.webp`.

For development, open this directory as a VS Code extension project. The compiler-side language server entry point is `noqeri lsp`; transport/client wiring is intentionally kept separate from the syntax package until the protocol surface is stabilized.
