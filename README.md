# LXcode

LXcode is an independent, open-source, Linux-native IDE for Apple Xcode projects. It reads and writes real `.xcodeproj` / `.xcworkspace` files, provides a source editor and navigator, orchestrates builds through [OSXCROSS](https://github.com/tpoechtrager/osxcross), and integrates with Git, language servers (LSP), and debug adapters (DAP).

LXcode is **not affiliated with, endorsed by, or derived from Apple Inc.** Xcode is a trademark of Apple Inc.

## Current status

Version 0.1.0 is a working first milestone, not a complete clone of Xcode.

Implemented:

- Qt 6 desktop interface inspired by Xcode's layout: toolbar, project navigator, tabbed editor, inspector, console, settings, light/dark themes.
- Source editor with Tree-sitter syntax highlighting, line numbers, folding indicators, find/replace, bracket matching, diagnostics underlines, and LSP completion/go-to-definition.
- Loss-minimizing OpenStep/`project.pbxproj` parser and serializer. Unknown keys, key order, quoting, and comments are preserved when fields are modified.
- Xcode project operations: targets, build phases, build settings, file/group add/rename/remove, schemes, workspaces, `Info.plist`, and `.xcconfig`.
- OSXCross discovery via `OSXCROSS_ROOT`, config files, and common install locations; SDK discovery; architecture-specific wrapper detection; compile/link argument generation; CMake toolchain file generation.
- Build graph with dependency validation, incremental checks, async execution, diagnostics parsing, clean/build commands, and cancellable builds.
- Git status, staging, commit, branches, checkout, log, diff, and remote listing.
- JSON-RPC transport used by external LSP servers and DAP debug adapters.
- Generator for a Linux SDK bundle that Xcode on macOS can consume: `SDKSettings.plist`, a platform bundle, an xctoolchain with clang/lld wrappers, an `.xcconfig`, and an installer. No Apple SDK is included.
- Functional CLI: `lxcode`, `open`, `build`, `clean`, `toolchains`, `xcode-sdk`, `doctor`, `info`, `create`.
- Automated unit tests and an offscreen GUI smoke test.

Not implemented (see [COMPATIBILITY.md](COMPATIBILITY.md)):

- Xcode extensions, simulators, device management, provisioning, signing, Xcode Cloud, App Store Connect.
- Full Apple framework/toolchain compatibility or running macOS binaries on Linux.
- Xcode's Interface Builder, Asset Catalog editor, SwiftUI preview, and storyboard designer.

## Quick start

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/lxcode --version
./build/lxcode /path/to/MyApp.xcodeproj
```

If Qt 6 is installed in a non-standard location, see [BUILDING.md](BUILDING.md).

### Command line

```sh
lxcode open MyApp.xcodeproj
lxcode build MyApp.xcodeproj --configuration Release --architecture arm64 --toolchain 'OSXCross (osxcross)'
lxcode clean MyApp.xcodeproj
lxcode toolchains
lxcode doctor MyApp.xcodeproj
lxcode info MyApp.xcodeproj
```

`lxcode build` requires a usable OSXCross toolchain with an installed macOS SDK. A system compiler can be inspected in `lxcode toolchains`, but it cannot produce macOS binaries without an Apple SDK.

### Linux SDK bundle for Xcode

Separately from LXcode's own build backend, LXcode can generate a bundle that **Xcode on macOS** uses to cross-compile for Linux:

```sh
lxcode xcode-sdk create ~/LinuxBundle --name "Linux" --arch x86_64,arm64 \
  --sysroot /path/to/linux-sysroot
lxcode xcode-sdk doctor ~/LinuxBundle
lxcode xcode-sdk install ~/LinuxBundle --developer-dir "$(xcode-select -p)"
```

The bundle contains an `SDKSettings.plist`, `Linux.platform`, `Linux.xctoolchain` with per-architecture clang wrappers and LLVM binary-util wrappers, a ready-to-include `Linux.xcconfig`, and an `install.sh`. It contains **no Apple SDK files** — you supply a Linux sysroot and LLVM tools. See [BUILDING.md](BUILDING.md#linux-sdk-bundle-for-xcode).

## Documentation

- [BUILDING.md](BUILDING.md) — dependencies, CMake options, local Qt prefixes.
- [ARCHITECTURE.md](ARCHITECTURE.md) — modules and data flow.
- [COMPATIBILITY.md](COMPATIBILITY.md) — what is supported and what is intentionally out of scope.
- [ROADMAP.md](ROADMAP.md) — planned work.
- [CONTRIBUTING.md](CONTRIBUTING.md) — development workflow.
- [THIRD_PARTY.md](THIRD_PARTY.md) — vendored and external dependencies.
- [LICENSE](LICENSE) — MIT license.

## License

MIT. See [LICENSE](LICENSE). Third-party components retain their own licenses; see [THIRD_PARTY.md](THIRD_PARTY.md).
