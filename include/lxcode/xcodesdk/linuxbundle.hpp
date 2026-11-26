#pragma once
#include "lxcode/core/expected.hpp"

#include <filesystem>
#include <string>
#include <vector>

// Generates a Linux SDK bundle that Xcode on macOS can consume. The bundle
// contains an SDKSettings.plist, a platform bundle, an xctoolchain with
// clang/lld wrappers, and an .xcconfig for wiring it into a project.
//
// No Apple SDK, Apple code, or Apple assets are included. The user supplies a
// Linux sysroot (headers and libraries) separately.
namespace lxcode::xcodesdk {

constexpr const char* manifestFileName = "lxcode-linux-sdk.json";
constexpr const char* sdkName = "Linux.sdk";
constexpr const char* platformName = "Linux.platform";
constexpr const char* toolchainName = "Linux.xctoolchain";

struct Manifest {
    int version = 1;
    std::string name = "Linux";
    std::string identifier = "dev.lxcode.sdk.linux";
    std::string versionString = "1.0";
    std::vector<std::string> architectures = {"x86_64", "arm64"};
    std::filesystem::path sysroot; // absolute or relative to the bundle; empty => <bundle>/sysroot
    std::string clang;             // optional explicit clang path; empty => resolved on PATH at build time
    std::string clangxx;           // optional explicit clang++ path
    std::filesystem::path llvmBin; // optional directory containing ld.lld, llvm-ar, llvm-nm, ...
};

struct Validation {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    bool ok() const { return errors.empty(); }
};

// Maps an Xcode architecture name to a Linux LLVM target triple.
// x86_64 -> x86_64-unknown-linux-gnu, arm64/aarch64 -> aarch64-unknown-linux-gnu.
std::string llvmTarget(const std::string& architecture);

// Pure content generators. Paths written into these files are portable: the
// wrappers locate the bundle relative to their own directory, and the
// .xcconfig uses a LINUX_SDK_BUNDLE variable the user can override.
std::string sdkSettingsPlist(const Manifest& manifest);
std::string platformInfoPlist(const Manifest& manifest);
std::string toolchainInfoPlist(const Manifest& manifest);
std::string xcconfigContents(const std::filesystem::path& bundleRoot);
std::string installScriptContents();
std::string bundleReadme(const Manifest& manifest);

Expected<Manifest> readManifest(const std::filesystem::path& root);
Expected<void> writeManifest(const std::filesystem::path& root, const Manifest& manifest);
Expected<void> writeWrappers(const std::filesystem::path& root, const Manifest& manifest);

// Creates or updates a bundle in place. Existing sysroot contents and user
// files are never removed; generated files are overwritten.
Expected<std::filesystem::path> createBundle(const std::filesystem::path& root,
                                             const Manifest& manifest);

Validation validate(const std::filesystem::path& root);

// Writes an .xcconfig next to the bundle (or at outputPath) that configures an
// Xcode target to use it.
Expected<std::filesystem::path> generateXcconfig(const std::filesystem::path& root,
                                                 const std::filesystem::path& outputPath);

// Copies Linux.platform into <developerDir>/Platforms and Linux.xctoolchain
// into <developerDir>/Toolchains. Returns the paths that were (or would be)
// written. With dryRun no filesystem changes are made.
Expected<std::vector<std::filesystem::path>> install(const std::filesystem::path& root,
                                                     const std::filesystem::path& developerDir,
                                                     bool dryRun);

} // namespace lxcode::xcodesdk
