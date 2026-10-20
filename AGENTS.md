# Agent guidance

noqeri is a programming-language implementation. Before changing syntax, read `Grammar/noqeri.grammar.md`, `Doc/LANGUAGE_REFERENCE.md`, and `Include/noqeri/noqeri.hpp`.

Keep lexer, parser, type checker, NIR, interpreter/native backend and tests aligned. Do not create empty placeholder files. Public language spelling is `noqeri`, executable `noqeri`, source extension `.noqeri`, manifest `project.noqeri`, and lock file `noqeri.lock`.

Run build/tests for the target platform and `noqeri doctor .` before release-oriented changes.
