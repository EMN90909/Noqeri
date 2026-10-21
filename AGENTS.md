# Agent guidance

noqeri is a programming-language implementation. Before changing syntax, read `Grammar/noqeri.grammar.md`, `Doc/LANGUAGE_REFERENCE.md`, and `Include/noqeri/noqeri.hpp`.

Keep lexer, parser, type checker, NIR, interpreter/native backend and tests aligned. Do not create empty placeholder files. Public language spelling is `noqeri`, executable `noqeri`, source extension `.noqeri`, canonical manifest `project.nqon`, and lock file `noqeri.lock`. Legacy `project.noqeri` is migration compatibility, not the syntax to generate for new projects.

Run build/tests for the target platform and `noqeri doctor .` before release-oriented changes.

Beginner-surface changes must follow `Doc/SYNTAX_SIMPLICITY.md`: prefer one spelling per concept, inference only when sound, and measurable usability evidence before making comparative simplicity claims.
