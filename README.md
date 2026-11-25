# LXcode

LXcode is an independent, open-source, Linux-native IDE for Apple Xcode projects. It reads and writes real `.xcodeproj` / `.xcworkspace` files, provides a source editor and navigator, orchestrates builds through [OSXCROSS](https://github.com/tpoechtrager/osxcross) or a self-contained clang/lld SDK bundle, and integrates with Git, language servers (LSP), and debug adapters (DAP).

LXcode is **not affiliated with, endorsed by, or derived from Apple Inc.** Xcode is a trademark of Apple Inc.

## Current status

Version 0.1.0 is a working first milestone, not a complete clone of Xcode.

Implemented:

- Qt 6 desktop interface inspired by Xcode's layout: toolbar, project navigator, tabbed editor, inspector, console, settings, light/dark themes.
- Source editor with Tree-sitter syntax highlighting, line numbers, folding indicators, find/replace, bracket matching, diagnostics underlines, and LSP completion/go-to-definition.
- Loss-minimizing OpenStep/`project.pbxproj` parser and serializer. Unknown keys, key order, quoting, and comments are preserved when fields are modified.
- Xcode project operations: targets, build phases, build settings, file/group add/rename/remove, schemes, workspaces, `Info.plist`, and `.xcconfig`.
- OSXCross discovery via `OSXCROSS_ROOT`, config files, and common install locations; SDK discovery; architecture-specific wrapper detection; compile/link argument generation; CMake toolchain file generation.
- Self-contained macOS SDK bundles: generated clang/lld wrappers, a JSON manifest, automatic discovery, and per-architecture compile/link arguments. No Apple SDK is bundled; you supply your own.
- Build graph with dependency validation, incremental checks, async execution, diagnostics parsing, clean/build commands, and cancellable builds.
- Git status, staging, commit, branches, checkout, log, diff, and remote listing.
- JSON-RPC transport used by external LSP servers and DAP debug adapters.
- Functional CLI: `lxcode`, `open`, `build`, `clean`, `toolchains`, `sdk`, `doctor`, `info`, `create`.
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

`lxcode build` requires a usable macOS cross toolchain with an installed SDK — either OSXCross or a custom SDK bundle. A system compiler can be inspected in `lxcode toolchains`, but it cannot produce macOS binaries without an Apple SDK.

### Custom macOS SDK bundle

LXcode can generate a toolchain bundle that drives the host's `clang`/`clang++` with `lld` against a macOS SDK that you supply. It contains no Apple files:

```sh
lxcode sdk create ~/my-sdk-bundles/MacOS --name "My macOS SDK" --arch arm64,x86_64
# Put your legally obtained SDK at ~/my-sdk-bundles/MacOS/SDK/MacOSX*.sdk,
# or create the bundle with --sdk /path/to/MacOSX.sdk.
lxcode sdk doctor ~/my-sdk-bundles/MacOS
LXCODE_SDK_ROOT=~/my-sdk-bundles/MacOS lxcode toolchains
```

The bundle's `lxcode-sdk-bundle.json` manifest controls the compiler paths, SDK root, architectures, and deployment target. Regenerate its wrappers with the same `sdk create` command. Bundles under `~/.lxcode/sdk-bundles/` (or `LXCODE_SDK_BUNDLES`) are discovered automatically; `LXCODE_SDK_ROOT` and `MACOS_SDK_ROOT` are also probed. See [BUILDING.md](BUILDING.md#custom-sdk-bundle-setup).

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
