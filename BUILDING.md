# Building LXcode

## Requirements

- Linux x86_64 (AMD64) or AArch64 (ARM64)
- CMake 3.22+
- C++20 compiler (GCC 12+ or Clang 15+)
- Qt 6.2+ with Core, Widgets, Gui, Network, and Concurrent
- Neovim 0.9+ (`nvim`) — required by the GUI editor; the CLI does not need it
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

## Neovim

LXcode's GUI does not use a Qt text editor. Each editor tab launches a private headless Neovim process and renders its UI grid in Qt, so Neovim is a runtime requirement for the GUI.

```sh
# Debian/Ubuntu
sudo apt install neovim
# Fedora
sudo dnf install neovim
# Arch
sudo pacman -S neovim
```

LXcode looks for `nvim` in this order: the `editorNvimPath` setting in Settings, the `NVIM` environment variable, then `PATH`. Override the configuration file with `LXCODE_NVIM_CONFIG=/path/to/init.lua`; otherwise Neovim starts with `--clean` so its behavior is predictable.

LXcode speaks Neovim's msgpack-rpc protocol directly. Editing, syntax, motions, registers, and key handling come from Neovim itself. Qt supplies the tab chrome, completion popup, diagnostics, LSP/DAP wiring, and grid rendering.

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

## Linux SDK bundle for Xcode

This generator produces files for **Xcode running on macOS**; it is not part of LXcode's own build backend and cannot be exercised from a Linux host.

```sh
lxcode xcode-sdk create ~/LinuxBundle \
  --name "Linux" \
  --arch x86_64,arm64 \
  --sysroot /path/to/linux-sysroot
lxcode xcode-sdk doctor ~/LinuxBundle
lxcode xcode-sdk install ~/LinuxBundle --developer-dir /Applications/Xcode.app/Contents/Developer
```

`create` writes:

```text
LinuxBundle/
  lxcode-linux-sdk.json
  README.md
  install.sh
  Linux.xcconfig
  Linux.platform/Info.plist
  Linux.platform/Developer/SDKs/Linux.sdk/SDKSettings.plist
  Linux.xctoolchain/Info.plist
  Linux.xctoolchain/usr/bin/...     clang/clang++ wrappers and LLVM tool wrappers
  sysroot/                          empty placeholder; never overwritten by create
```

`--sysroot` may point at an existing Linux sysroot (`usr/include`, `usr/lib`); LXcode only references it and never creates or modifies it. Without `--sysroot`, populate `LinuxBundle/sysroot` yourself. All tool paths are resolved at build time from `PATH` unless `--clang`, `--clangxx`, or `--llvm-bin` are given. Equivalent runtime overrides are `LXCODE_LINUX_CLANG`, `LXCODE_LINUX_CLANGXX`, `LXCODE_LINUX_SYSROOT`, and `LXCODE_LINUX_LLVM_BIN`.

Wrappers strip Xcode's `-arch` flags (clang rejects them for Linux targets) and add `-fuse-ld=lld` only when linking. Include `Linux.xcconfig` in your target's configuration and set `LINUX_ARCH` to `x86_64` or `arm64`.

`install` copies `Linux.platform` and `Linux.xctoolchain` into the chosen developer directory; `--dry-run` prints the destinations without copying. `xcode-sdk doctor` validates structure, plists, wrappers, and the sysroot. Generated plist keys follow Xcode's documented SDK bundle layout; actual Xcode behavior must be verified on macOS.

## Testing on ARM64

The code avoids architecture-specific assembly and uses `CMAKE_SYSTEM_PROCESSOR` for the host architecture label. Build and run the same CTest suite on AArch64. OSXCross must be built for the host architecture and provide wrappers for the requested Apple architecture (`arm64` or `x86_64`).
