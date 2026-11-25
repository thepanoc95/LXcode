# Architecture

LXcode is a set of small C++20 static libraries with a thin CLI and Qt GUI on top. Modules communicate through plain value types and `Expected<T>` results; there is no global application state in the core libraries.

## Modules

| Directory | Library | Responsibility |
| --- | --- | --- |
| `src/core` | `lxcode_core` | Expected, strings, filesystem, process execution, JSON-RPC framing. |
| `src/project` | `lxcode_project` | OpenStep/PBX AST, XML, plists, xcconfig, workspaces, schemes, high-level `XcodeProject`. |
| `src/toolchains` | `lxcode_toolchains` | OSXCross and custom SDK bundle discovery, SDK/wrapper detection, compile/link arguments, registry. |
| `src/build` | `lxcode_build` | Build graph, validation, topological ordering, async execution, diagnostics, CMake toolchain generation. |
| `src/sourcecontrol` | `lxcode_sourcecontrol` | Git commands and typed status/branch/log/diff results. |
| `src/lsp` | `lxcode_lsp` | Language Server Protocol client over length-framed JSON-RPC. |
| `src/debug` | `lxcode_debug` | Debug Adapter Protocol client. |
| `src/config` | `lxcode_config` | JSON settings and recent projects. |
| `src/diagnostics` | `lxcode_diagnostics` | Issue collection and project/toolchain inspection. |
| `src/gui` | `lxcode_gui` | Qt main window, navigator, editor, inspector, console, settings, Tree-sitter highlighter. |
| `src/cli` | `lxcode` | Command-line interface. |

## Project compatibility strategy

`project.pbxproj` is an OpenStep-style property list. Instead of parsing it into a fixed schema and regenerating it, LXcode parses it into an ordered AST (`PbxValue`, `PbxEntry`) that stores:

- entry and dictionary key order,
- quoted versus bare strings,
- leading and trailing comments,
- unknown keys and object classes.

Mutations modify that AST in place, so fields LXcode does not understand survive a save. `XcodeProject` provides convenience operations on top of the AST without flattening it.

## Build flow

1. `XcodeProject` exposes targets, phases, file references, and resolved build settings.
2. `BuildController::buildGraphForProject` turns source/resource/script phases into `BuildCommand`s.
3. `BuildGraph` validates references and computes a topological order.
4. `BuildController` runs commands asynchronously using `runProcess`, emits events, parses compiler diagnostics, and skips up-to-date outputs based on timestamps.

Wrappers are selected per architecture. A `Toolchain` carries a `kind` (`OSXCross`, `CustomSdk`, `System`, or `CMake`), resolved tool paths, SDK root, and architectures. `customsdk::createBundle` writes a manifest, README, and generated clang/lld wrappers; `customsdk::detect` reads that manifest back into a `Toolchain`. `generateToolchainFile` emits a CMake toolchain file so external CMake-based dependencies can use the same SDK and wrappers; `generateOsxcrossToolchainFile` is a compatibility alias.

## GUI flow

`MainWindow` owns the project, toolchain registry, editor tabs, docks, and async clients. Worker threads emit Qt signals with queued connections so widget updates always happen on the GUI thread. Language servers and debug adapters are external processes; LXcode supplies the client protocol, not the tools themselves.

Syntax highlighting uses a vendored Tree-sitter runtime and C grammar. C++ and Objective-C files currently reuse the C grammar, which provides useful but incomplete highlighting.

## Error handling

Core APIs return `Expected<T>`. Failures carry human-readable messages displayed in CLI output, status bars, or message boxes. Invalid toolchains and unsupported project features are surfaced as diagnostics rather than silently ignored.
