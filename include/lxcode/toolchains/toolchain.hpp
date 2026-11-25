#pragma once
#include "lxcode/core/expected.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace lxcode {

enum class ToolchainKind { OSXCross, System, CMake };

struct Toolchain {
    ToolchainKind kind = ToolchainKind::System;
    std::string name;
    std::filesystem::path root;
    std::map<std::string, std::filesystem::path> tools; // clang, clang++, ld, ar, lipo, otool, nm
    std::vector<std::filesystem::path> sdks;
    std::filesystem::path sdk;
    std::vector<std::string> architectures;
    std::string version;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;

    bool valid() const { return errors.empty(); }
    bool supports(const std::string& architecture) const;
    const std::filesystem::path* tool(const std::string& role) const;
};

namespace osxcross {

std::vector<std::filesystem::path> candidateRoots();
Expected<Toolchain> detect(const std::filesystem::path& root);

// Chooses the actual installed wrapper for an architecture and role. Returns
// an empty path if the selected toolchain does not provide that tool.
std::filesystem::path toolFor(const Toolchain& toolchain, const std::string& architecture,
                              const std::string& role);

struct CompileOptions {
    std::string architecture = "arm64";
    std::string standard = "c++20";
    std::string deploymentTarget = "11.0";
    std::vector<std::string> includeDirectories;
    std::vector<std::string> defines;
    std::vector<std::string> flags;
};

struct LinkOptions {
    std::string architecture = "arm64";
    std::string deploymentTarget = "11.0";
    std::vector<std::string> libraryDirectories;
    std::vector<std::string> frameworks;
    std::vector<std::string> frameworkDirectories;
    std::vector<std::string> libraries;
    std::vector<std::string> flags;
};

std::vector<std::string> compileArguments(const Toolchain& toolchain,
                                          const std::filesystem::path& source,
                                          const std::filesystem::path& output,
                                          const CompileOptions& options);
std::vector<std::string> linkArguments(const Toolchain& toolchain,
                                       const std::vector<std::filesystem::path>& objects,
                                       const std::filesystem::path& output,
                                       const LinkOptions& options);
std::vector<std::string> lipoArguments(const Toolchain& toolchain,
                                       const std::vector<std::filesystem::path>& slices,
                                       const std::filesystem::path& output);

} // namespace osxcross

class ToolchainRegistry {
public:
    ToolchainRegistry& add(Toolchain toolchain);
    ToolchainRegistry& addPath(const std::filesystem::path& root);
    std::vector<Toolchain> detect();
    std::vector<Toolchain> toolchains() const { return toolchains_; }
    Toolchain* find(const std::string& name);
    const Toolchain* find(const std::string& name) const;
    Expected<Toolchain> selected() const;
    void setSelected(std::string name);
    std::string selectedName() const { return selected_; }
    Expected<void> loadConfiguration(const std::filesystem::path& path);
    Expected<void> saveConfiguration(const std::filesystem::path& path) const;

private:
    std::vector<Toolchain> toolchains_;
    std::vector<std::filesystem::path> extraRoots_;
    std::string selected_;
};

std::string toolchainKindName(ToolchainKind kind);

} // namespace lxcode
