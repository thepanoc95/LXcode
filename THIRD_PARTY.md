# Third-party components

LXcode includes or depends on the following components. Each retains its own license.

## Vendored in this repository

### nlohmann/json

- Used by: config settings, toolchain registry, JSON-RPC payloads.
- License: MIT.
- Source: https://github.com/nlohmann/json
- Location: `third_party/nlohmann`

### Tree-sitter runtime and C grammar

- Used by: project metadata parsing helpers and diagnostics. Neovim now owns editor syntax highlighting.
- License: MIT.
- Sources: https://github.com/tree-sitter/tree-sitter and https://github.com/tree-sitter/tree-sitter-c
- Location: `third_party/tree-sitter`

## External build/runtime dependencies

- **Qt 6** (Core, Gui, Widgets, Network, Concurrent) — LGPL-3.0 or commercial. https://www.qt.io/
- **Neovim** — Apache-2.0. Required at runtime by the GUI editor; not bundled. https://neovim.io/
- **CMake** — BSD-3-Clause. https://cmake.org/
- **Git** — GPL-2.0. Used as an external command for source control. https://git-scm.com/
- **OSXCROSS** — MIT. Optional external cross-compilation toolchain; not bundled. https://github.com/tpoechtrager/osxcross
- **clangd** — Apache-2.0. Optional external LSP server; not bundled. https://clangd.llvm.org/
- **lldb-dap** — Apache-2.0 with LLVM exceptions. Optional external DAP adapter; not bundled. https://lldb.llvm.org/
- **macOS SDK** — Provided by Apple under its own license. Not bundled, not redistributed.

## Apple trademarks

Xcode, macOS, Apple, and related marks are trademarks of Apple Inc. LXcode is an independent project and is not affiliated with or endorsed by Apple. No Apple logos or proprietary assets are included.
