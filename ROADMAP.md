# Roadmap

This roadmap describes planned work, not shipped features. See [COMPATIBILITY.md](COMPATIBILITY.md) for current status and explicit non-goals.

## 0.2 — Compatibility hardening

- Broaden OpenStep/PBX coverage for modern Xcode 15/16 project fields.
- Add focused regression fixtures from real-world `.xcodeproj` files.
- Improve `.xcuserdatad`, `.xcscmblueprint`, and shared scheme preservation.
- More accurate C++ / Objective-C Tree-sitter grammars in the editor.
- Better diagnostics for malformed or partially supported project files.

## 0.3 — Build and toolchain improvements

- Universal binary (fat) build orchestration with `lipo` verification.
- Improved incremental build caching and dependency tracking.
- OSXCross version and SDK compatibility checks in `doctor`.
- Better handling of custom build phases and shell script phases.

## 0.4 — Editor and tooling polish

- Multi-cursor editing and richer code folding.
- LSP rename, signature help, and workspace symbol search.
- DAP breakpoint condition editing and variable inspection improvements.
- Git history graph and per-file blame in the UI.

## Later / not committed

- Alternative cross-compilation backends alongside OSXCross.
- Additional platform targets where an open SDK is available.
- Stable plugin API for third-party contributions.

## Out of scope

Xcode extensions, simulators, device management, provisioning, signing, Xcode Cloud, App Store Connect, and Apple-proprietary services are not planned.
