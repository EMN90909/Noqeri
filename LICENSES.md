# Noqeri source licensing by component — v0.1.4

The repository is **not uniformly licensed**. Under the project-maintained distribution policy, the **compiler and toolchain** are GPL-3.0-only, while independently redistributable **first-party libraries and reusable application-facing components** are Apache-2.0. This file is a scope declaration, **not a waiver of third-party or prior contributors' rights**. A separate file copyright/license notice has priority. The full GPL-3.0-only text is in [LICENSE](LICENSE); the full Apache-2.0 text is in [LICENSES/Apache-2.0.txt](LICENSES/Apache-2.0.txt).

## GPL-3.0-only (compiler, build system, toolchain)

- `Compiler/**`, including the C++17 bootstrap compiler, native web bundler, source parsers, backends, embedded compiler resources and self-hosted compiler modules.
- The native NQRI execution toolchain (`Runtime/native/nqri_vm.cpp` and `Runtime/native/nqri_vm_part*.inc`), build/release packaging infrastructure, `CMakeLists.txt`, `cmake/**`, compiler command-line drivers and tooling that are not reusable independent libraries.
- `Include/noqeri/{ast,compiler,nir,nqri,version,workspace,diagnostics}.hpp` and other interfaces consisting principally of compiler/toolchain internals.
- `LICENSE` and GPL-labeled individual files remain GPL. No exception to the GPL for linking against the compiler or VM is implied.

## Apache-2.0 (independent first-party libraries and reusable code)

- The project-authored reusable modules in `Lib/**`, `Modules/**` and `Objects/**`, including `Lib/web/ui.noqeri`, `Lib/std/http/**`, and `Lib/std/websocket.noqeri`.
- The separate browser runtime, theme utilities, reusable graphics/UI modules, and host-library interfaces in `Runtime/**` and `Include/**` **where those files are independent library code rather than the GPL toolchain**, subject to explicit file-level notices.
- Original examples and documentation, to the extent they are not derived from GPL-only files or independently licensed assets.
- New standalone library interfaces should carry `SPDX-License-Identifier: Apache-2.0`; new compiler/toolchain source should carry `SPDX-License-Identifier: GPL-3.0-only`.

**Important:** This applies only to code for which the Noqeri project has authority to choose these terms. It cannot retrospectively relicense code whose copyright holders have not authorized Apache-2.0. Contributions made under GPL-only without appropriate relicensing permission **remain GPL-3.0-only**; the directory statement alone does not convert them. Libraries containing GPL-derived code, third-party vendored modules, assets, or code imported from GPL-only NoeSQL retain applicable licensing obligations even if called from an Apache-facing API. Before certifying any binary as Apache-only, audit its complete source provenance and linkage.

## Packaging and redistribution

- Distribute **both** `LICENSE` and `LICENSES/Apache-2.0.txt` in mixed-license SDK/source archives, and maintain notices and third-party inventories.
- Compiler and toolchain packages and combined executables that include GPL-covered compiler code continue to carry their GPL obligations. Independent Apache-licensed libraries can be distributed under Apache-2.0 **when their individual provenance permits**; using them from an application does not by itself impose GPL obligations from the separate compiler executable.
- The legacy `cmake/NoqeriLicense.rtf` and installer UI GPL resource describe the toolchain; they do not override independently Apache-licensed libraries.
- This scope declaration is not a completed legal copyright audit. Verify copyright-holder consent for any GPL-to-Apache relicensing before making an unqualified public license assertion.

License identifiers: [GPL-3.0-only](https://spdx.org/licenses/GPL-3.0-only.html) and [Apache-2.0](https://spdx.org/licenses/Apache-2.0.html).
