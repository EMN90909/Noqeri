# Noqeri release verification — 2026-10-17

## Verified source changes
- POSIX and PowerShell bootstrap scripts and Stage-2 CLI now use the JavaScript module extension .mjs consistently.
- A new Stage-2 ESM ABI validator checks JavaScript importability and required compiler exports; bootstrap fails closed on incompatible generated modules.
- Noqeri README documents current Stage-2 commands and warns that binary NQO2 is not an importable ES module.
- noqeri-web documentation corrects generated browser artifact extensions and clarifies that source download is not a verified compiler distribution.
- noqeri-registry now includes an offline catalog validator for coordinates, versions, paths, duplicates, protocol and yanked flags.

## Critical release blockers remaining

| Subsystem | Source present | What still needs executable verification |
| --- | --- | --- |
| Type system | semantic_program.nqr, types.nqr, type_ref.nqr, generics.nqr | Valid and invalid type matrix; casts, overflow, lifetime, diagnostics |
| Memory safety | safety and lifetime implementations | Aliasing, escaping borrows, null, bounds and sanitizer/fuzz testing |
| Frontend | Lexer, parser, semantic pipeline | Full grammar corpus, import resolution, source diagnostics and fuzzing |
| Optimizer | Folding, simplification and dead-code passes | Differential optimizer-on/off execution, overflow and control flow |
| Native backends | ELF, COFF, Mach-O and linker sources | Produce, link and execute real binaries for each claimed target |
| Package manager | Resolver, lockfile and SHA-256 cache logic | Online/offline dependency correctness, tampering, partial installation recovery |
| Distribution | Source installers and registry | Clean-machine installs, platform-specific smoke tests and signed artifacts |
| Bootstrap | Stage-2 compiler source and gate | Trusted seed builds Stage-1, Stage-1 builds Stage-2, equivalence proven |
| Web | Restricted JS backend | Typed NIR code generation and real browser parity |

## Conclusion

Stable v1.0.0 is BLOCKED. No successful complete compiler bootstrap, native executable, memory-safety proof, full optimizer equivalence, or distribution installation is established by this update. The fixes improve correctness and make verification fail closed; they do not establish comprehensive compiler maturity.

Suggested local commands once a trusted seed/compiler exists:
- node scripts/verify-stage2-module.mjs
- sh scripts/test_core.sh
- node scripts/bootstrap-stage2.mjs (requires configured trusted stages)
- node --test tests/web-compiler.test.mjs tests/web-project.test.mjs
- node scripts/verify-index.mjs (in noqeri-registry)
- npm run test:source; npm run build; npm run smoke (in noqeri-web)

No GitHub Actions were used.
