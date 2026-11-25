# Building LXcode

## Requirements

- Linux x86_64 (AMD64) or AArch64 (ARM64)
- CMake 3.22+
- C++20 compiler (GCC 12+ or Clang 15+)
- Qt 6.2+ with Core, Widgets, Gui, Network, and Concurrent
- nlohmann/json (vendored in `third_party/nlohmann`)
- Tree-sitter runtime and C grammar (vendored in `third_party/tree-sitter`)
- Git (for Git integration and commit-based builds of the source tree)
- Optional: OSXCross with an installed macOS SDK for real macOS cross-builds

The CLI can be built without Qt:

```sh
cmake -S . -B build-cli -DLXCODE_BUILD_GUI=OFF -DLXCODE_BUILD_TESTS=ON
cmake --build build-cli -j
ctest --test-dir build-cli --output-on-failure
```

## Full build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Install:

```sh
cmake --install build --prefix /usr/local
```

This installs `lxcode`, `lxcode-gui`, and theme resources to `share/lxcode`.

## Non-standard Qt prefix

If Qt is unpacked locally rather than installed system-wide:

```sh
export QT_PREFIX=/home/milo/.local/opt/lxcode-deps/usr/lib/x86_64-linux-gnu/cmake
export LD_LIBRARY_PATH=/home/milo/.local/opt/lxcode-deps/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH

cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
  -DLXCODE_BUILD_GUI=ON \
  -DLXCODE_BUILD_TESTS=ON
cmake --build build -j
LD_LIBRARY_PATH="$LD_LIBRARY_PATH" ctest --test-dir build --output-on-failure
```

`LD_LIBRARY_PATH` is needed for Qt tools such as `moc` and for running the GUI when the Qt libraries are not registered with the dynamic linker.

## CMake options

| Option | Default | Meaning |
| --- | --- | --- |
| `LXCODE_BUILD_GUI` | `ON` | Build the Qt 6 desktop interface and `lxcode-gui`. |
| `LXCODE_BUILD_TESTS` | `ON` | Build unit tests and register CTest tests. |
| `LXCODE_USE_SYSTEM_DEPS` | `OFF` | Reserved for distro builds that use system-provided JSON/Tree-sitter packages. |

## OSXCross setup

Set `OSXCROSS_ROOT` to an OSXCross installation, or add roots in Settings / `settings.json`:

```json
{
  "toolchainRoots": ["/opt/osxcross"],
  "toolchain": "OSXCross (osxcross)"
}
```

Expected layout:

```text
$OSXCROSS_ROOT/bin/*-apple-darwin-clang
$OSXCROSS_ROOT/bin/*-apple-darwin-clang++
$OSXCROSS_ROOT/bin/lipo
$OSXCROSS_ROOT/SDK/MacOSX*.sdk
```

Run `lxcode toolchains` to verify discovery. Run `lxcode doctor /path/to/App.xcodeproj` to see project and toolchain diagnostics.

## Testing on ARM64

The code avoids architecture-specific assembly and uses `CMAKE_SYSTEM_PROCESSOR` for the host architecture label. Build and run the same CTest suite on AArch64. OSXCross must be built for the host architecture and provide wrappers for the requested Apple architecture (`arm64` or `x86_64`).
