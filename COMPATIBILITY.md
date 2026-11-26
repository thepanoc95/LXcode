# Compatibility and scope

LXcode is an independent reimplementation. It is not Xcode, is not affiliated with Apple Inc., and does not include Apple code, assets, SDKs, or proprietary frameworks.

## Project file compatibility

LXcode reads and writes real Xcode project files:

- `*.xcodeproj/project.pbxproj` (OpenStep-style property list)
- `*.xcworkspace/contents.xcworkspacedata`
- `*.xcscheme`, `*.xcconfig`, `Info.plist` (XML and OpenStep plists)
- `*.xcuserdatad` and `*.xcscmblueprint` are preserved as opaque data on disk.

The PBX parser is intentionally loss-minimizing: key order, comments, quoting, unknown object classes, and unknown build settings survive a round-trip. When LXcode writes a project it modifies the parsed AST in place rather than regenerating a canonical file. This makes files safe to open in Xcode.

**Limits:** Very new or unusual Xcode file formats may contain fields LXcode does not interpret. Those fields are preserved but not surfaced in the UI. Invalid `project.pbxproj` files are rejected with diagnostics.

## Build compatibility

LXcode uses OSXCross as its primary macOS cross-compilation backend. Real macOS builds require a properly installed OSXCross toolchain with a macOS SDK. LXcode does not ship an SDK and cannot produce macOS binaries without one.

Supported build inputs:

- PBX source, framework, and resource build phases
- Build settings from project and target configurations, including `.xcconfig` includes
- `arm64`, `x86_64`, and universal (`lipo`) outputs
- External CMake-based subprojects via a generated OSXCross toolchain file

**Limits:** LXcode does not reproduce every Xcode build rule, sandbox behavior, or Apple toolchain feature. Complex custom build scripts may need manual adjustment. Running or testing macOS binaries requires a macOS host or an emulator, neither of which is provided.

### Linux SDK bundle for Xcode

`lxcode xcode-sdk` generates a bundle for Xcode on macOS that cross-compiles ELF binaries for Linux. It contains SDK/plist metadata, wrapper scripts, an `.xcconfig`, and an installer, but **no Apple SDK or Apple code**. A Linux sysroot and LLVM tools are supplied by the user.

**Limits:** Xcode is designed for Apple platforms. The bundle covers compiler/linker invocation and build settings, but Swift, Apple frameworks, code signing, simulators, and running the resulting ELF binaries on macOS are out of scope. The generated files are structurally validated on Linux; integration with Xcode itself has not been verified on a macOS host.

## Editor and tooling

- Syntax highlighting uses a vendored Tree-sitter runtime with the C grammar. C++, Objective-C, and Objective-C++ reuse the C grammar and therefore have incomplete highlighting.
- Language features (completion, go-to-definition, hover, diagnostics) come from an external `clangd` or other LSP server configured by the user.
- Debugging uses an external DAP adapter such as `lldb-dap`. LXcode implements the client protocol, not the debugger.

## Git

Git support shells out to the installed `git` executable. Standard working-copy workflows (status, stage, commit, branches, log, diff, remotes) are supported. Merge conflict markers are surfaced in the UI.

## Explicitly out of scope

The following are **not implemented** and are not planned for the 0.x series:

- Xcode extensions and plugin APIs
- iOS/tvOS/watchOS simulators and physical device management
- Provisioning profiles, code signing, and Apple Developer services
- Xcode Cloud and App Store Connect integration
- Interface Builder, storyboard/asset catalog visual editors, SwiftUI previews
- Running macOS applications on Linux without an external compatibility layer
- Editing Apple-proprietary file formats that require Apple libraries

## Platform support

- Linux x86_64 (AMD64) and AArch64 (ARM64)
- Qt 6.2+ for the desktop UI
- The CLI builds without Qt
