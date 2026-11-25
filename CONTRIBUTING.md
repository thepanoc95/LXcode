# Contributing to LXcode

Thanks for helping improve an open, Linux-native Xcode-compatible IDE.

## Development setup

1. Install the dependencies in [BUILDING.md](BUILDING.md).
2. Configure with tests enabled and your Qt prefix if needed:
   ```sh
   cmake -S . -B build -DLXCODE_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
   cmake --build build -j
   ctest --test-dir build --output-on-failure
   ```
3. For a CLI-only iteration loop:
   ```sh
   cmake -S . -B build-cli -DLXCODE_BUILD_GUI=OFF -DLXCODE_BUILD_TESTS=ON
   cmake --build build-cli -j
   ctest --test-dir build-cli --output-on-failure
   ```

## Code style

- C++20, no compiler-specific extensions in core libraries.
- Prefer `Expected<T>` for recoverable errors; avoid exceptions across module boundaries.
- Keep PBX/OpenStep mutations AST-based so unknown metadata is preserved.
- Use existing module boundaries in `src/`; do not add cross-module includes that bypass public headers in `include/`.
- Qt code runs widget updates on the GUI thread; worker threads communicate through signals with queued connections.

## Tests

- Add a focused unit test under `tests/` for each behavioral change.
- Project file tests should verify that unknown keys, comments, and quoting survive a parse/save round-trip.
- CLI changes should be reflected in `tests/cli_doctor.cmake` or a new CTest entry.
- GUI changes should keep `lxcode-gui --smoke-test` working under `QT_QPA_PLATFORM=offscreen`.

## Pull requests

- Explain the Xcode compatibility behavior being fixed or added.
- Include before/after `project.pbxproj` snippets when changing serialization.
- Do not add Apple code, assets, SDKs, or trademarked logos.
- Keep documentation in sync with behavior; do not claim unsupported features.

## Reporting bugs

Include the LXcode version, Linux distribution and architecture, Qt version, the smallest `.xcodeproj` that reproduces the issue, and the relevant `lxcode doctor` output.
