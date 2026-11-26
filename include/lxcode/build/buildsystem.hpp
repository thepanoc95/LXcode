#pragma once
#include "lxcode/core/expected.hpp"
#include "lxcode/project/xcodeproject.hpp"
#include "lxcode/toolchains/toolchain.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lxcode {

struct Diagnostic {
    enum class Severity { Note, Warning, Error };
    Severity severity = Severity::Note;
    std::string file;
    int line = 0;
    int column = 0;
    std::string message;
    std::string raw;

    std::string severityName() const;
};

std::vector<Diagnostic> parseDiagnostics(const std::string& output);

enum class CommandKind { Compile, Link, Copy, Script, Configure, Generic };

struct BuildCommand {
    CommandKind kind = CommandKind::Generic;
    std::string description;
    std::string executable;
    std::vector<std::string> arguments;
    std::filesystem::path workingDirectory;
    std::filesystem::path output;
    std::vector<std::filesystem::path> inputs;
};

struct BuildNode {
    std::string id;
    std::string name;
    std::vector<std::string> dependencies;
    std::vector<BuildCommand> commands;
};

class BuildGraph {
public:
    void add(BuildNode node);
    std::vector<BuildNode> nodes() const { return nodes_; }
    Expected<std::vector<std::string>> topologicalOrder() const;
    std::vector<std::string> validate() const;

private:
    std::vector<BuildNode> nodes_;
};

struct BuildRequest {
    std::filesystem::path project;
    std::string target;
    std::string configuration = "Debug";
    std::string architecture = "arm64";
    Toolchain toolchain;
    std::filesystem::path buildDirectory;
    std::vector<std::string> environmentVariables;
    bool clean = false;
};

struct BuildEvent {
    enum class Type { Started, Command, Output, Diagnostic, Finished, Cancelled };
    Type type = Type::Output;
    std::string text;
    Diagnostic diagnostic;
    int commandIndex = 0;
    int commandCount = 0;
};

struct BuildResult {
    bool success = false;
    bool cancelled = false;
    int commandCount = 0;
    int skippedCount = 0;
    std::vector<Diagnostic> diagnostics;
    std::string log;
    std::chrono::milliseconds duration{0};
};

class BuildController {
public:
    explicit BuildController(BuildRequest request);
    ~BuildController();
    BuildController(const BuildController&) = delete;
    BuildController& operator=(const BuildController&) = delete;

    void start(std::function<void(const BuildEvent&)> onEvent,
               std::function<void(const BuildResult&)> onFinished);
    bool running() const;
    void cancel();
    BuildResult wait();
    BuildRequest& request() { return request_; }
    BuildGraph& graph() { return graph_; }

    static Expected<BuildGraph> buildGraphForProject(const XcodeProject& project,
                                                     const std::string& targetId,
                                                     const std::string& configuration,
                                                     const std::string& architecture,
                                                     const Toolchain& toolchain,
                                                     const std::filesystem::path& buildDirectory);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    BuildRequest request_;
    BuildGraph graph_;
};

// Generates a CMake toolchain file that forwards to the selected OSXCross tools.
Expected<std::filesystem::path> generateOsxcrossToolchainFile(
    const Toolchain& toolchain, const std::string& architecture,
    const std::filesystem::path& outputPath);

} // namespace lxcode
