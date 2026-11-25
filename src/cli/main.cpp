#include "lxcode/build/buildsystem.hpp"
#include "lxcode/config/settings.hpp"
#include "lxcode/core/filesystem.hpp"
#include "lxcode/core/process.hpp"
#include "lxcode/core/strings.hpp"
#include "lxcode/diagnostics/issues.hpp"
#include "lxcode/project/workspace.hpp"
#include "lxcode/project/xcodeproject.hpp"
#include "lxcode/sourcecontrol/git.hpp"
#include "lxcode/toolchains/toolchain.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace lxcode;

void printHelp() {
    std::cout << R"(LXcode — an independent, Linux-native Xcode-compatible IDE

Usage:
  lxcode                       Launch the desktop IDE
  lxcode open <project>        Open a project or workspace in the desktop IDE
  lxcode build <project>       Build a project or workspace
  lxcode clean <project>       Clean build products
  lxcode toolchains            List detected toolchains and SDKs
  lxcode doctor [path]         Report IDE/toolchain/project diagnostics
  lxcode info <project>        Print project targets, files and schemes
  lxcode create <dir> <name>   Create a new .xcodeproj in <dir>
  lxcode --version             Print version information
  lxcode --help                Print this help

Options for build/clean:
  --configuration <name>       Debug (default) or Release
  --architecture <arch>        arm64 (default) or x86_64
  --target <name>              Target name or identifier
  --toolchain <name>           Use a specific configured toolchain
)";
}

std::string optionValue(const std::vector<std::string>& args, const std::string& name,
                        std::string fallback) {
    const auto it = std::find(args.begin(), args.end(), name);
    if (it == args.end() || std::next(it) == args.end()) return fallback;
    return *std::next(it);
}

bool hasOption(const std::vector<std::string>& args, const std::string& name) {
    return std::find(args.begin(), args.end(), name) != args.end();
}

Expected<std::filesystem::path> projectPath(const std::string& argument) {
    std::filesystem::path path(argument);
    if (!fs::exists(path)) return Expected<std::filesystem::path>::failure("path not found: " + argument);
    if (std::filesystem::is_directory(path) && path.extension() == ".xcworkspace")
        return path / "contents.xcworkspacedata";
    if (std::filesystem::is_directory(path) && path.extension() == ".xcodeproj")
        return path / "project.pbxproj";
    return path;
}

Expected<std::filesystem::path> resolveProject(const std::string& argument) {
    std::filesystem::path path(argument);
    if (!fs::exists(path)) return Expected<std::filesystem::path>::failure("path not found: " + argument);
    if (path.extension() == ".xcworkspace") {
        auto workspace = XcWorkspace::open(path);
        if (!workspace) return Expected<std::filesystem::path>::failure(workspace.error());
        const auto projects = workspace->projectPaths();
        if (projects.empty())
            return Expected<std::filesystem::path>::failure("workspace has no project references");
        return projects.front() / "project.pbxproj";
    }
    return projectPath(argument);
}

ToolchainRegistry configuredRegistry(const Settings& settings,
                                     const std::string& selectedOverride = {}) {
    ToolchainRegistry registry;
    for (const auto& root : settings.stringList("toolchainRoots")) registry.addPath(root);
    auto configPath = settings.string("toolchainConfig");
    if (!configPath.empty()) registry.loadConfiguration(configPath);
    registry.detect();
    const auto configured = settings.string("toolchain");
    registry.setSelected(selectedOverride.empty() ? configured : selectedOverride);
    if (registry.selected()) return registry;
    const auto available = registry.toolchains();
    for (const auto& item : available) {
        if (item.kind == ToolchainKind::OSXCross && item.valid()) {
            registry.setSelected(item.name);
            break;
        }
    }
    return registry;
}

int launchGui(const std::string& argument) {
    auto path = which("lxcode-gui");
    if (path.empty()) {
        const auto local = std::filesystem::path("/usr/local/bin/lxcode-gui");
        if (fs::exists(local)) path = local.string();
    }
    if (path.empty()) {
        std::cerr << "lxcode-gui is not installed. Build the GUI with -DLXCODE_BUILD_GUI=ON.\n";
        return 1;
    }
    std::vector<std::string> args = {path};
    if (!argument.empty()) args.push_back(argument);
    auto pid = fork();
    if (pid == 0) {
        std::vector<char*> argv;
        for (auto& arg : args) argv.push_back(arg.data());
        argv.push_back(nullptr);
        execvp(path.c_str(), argv.data());
        _exit(127);
    }
    if (pid < 0) {
        std::cerr << "cannot start the desktop interface\n";
        return 1;
    }
    std::cout << "Launched LXcode (pid " << pid << ")\n";
    return 0;
}

int commandInfo(const std::string& argument) {
    auto path = resolveProject(argument);
    if (!path) { std::cerr << path.error() << '\n'; return 1; }
    auto project = XcodeProject::open(*path);
    if (!project) { std::cerr << project.error() << '\n'; return 1; }
    std::cout << "Project: " << project->name() << '\n';
    std::cout << "File:    " << project->path().string() << '\n';
    std::cout << "Targets:\n";
    for (const auto& target : project->targets()) {
        std::cout << "  - " << target.name << " [" << target.isa << "] " << target.productType << '\n';
        for (const auto& phase : target.phases)
            std::cout << "      " << phase.isa << ": " << phase.fileRefs.size() << " files\n";
    }
    std::cout << "Schemes:\n";
    for (const auto& scheme : project->schemes())
        std::cout << "  - " << scheme.name() << " (" << scheme.buildConfiguration("BuildAction") << ")\n";
    std::cout << "Diagnostics:\n";
    const auto issues = inspectProject(*project);
    if (issues.empty()) std::cout << "  none\n";
    for (const auto& issue : issues)
        std::cout << "  [" << static_cast<int>(issue.severity) << "] " << issue.message << '\n';
    return 0;
}

int commandBuild(const std::string& argument, bool clean, const std::vector<std::string>& args) {
    auto path = resolveProject(argument);
    if (!path) { std::cerr << path.error() << '\n'; return 1; }
    auto project = XcodeProject::open(*path);
    if (!project) { std::cerr << project.error() << '\n'; return 1; }

    Settings settings;
    settings.load();
    auto registry = configuredRegistry(settings, optionValue(args, "--toolchain", {}));
    auto selected = registry.selected();
    if (!selected) { std::cerr << selected.error() << '\n'; return 1; }
    for (const auto& warning : selected->warnings)
        std::cout << "warning: " << warning << '\n';

    const auto configuration = optionValue(args, "--configuration", "Debug");
    const auto architecture = optionValue(args, "--architecture", "arm64");
    BuildRequest request;
    request.project = *path;
    request.configuration = configuration;
    request.architecture = architecture;
    request.toolchain = *selected;
    request.clean = clean;
    request.buildDirectory = project->directory() / "build" / configuration / architecture;
    if (!optionValue(args, "--build-dir", {}).empty())
        request.buildDirectory = optionValue(args, "--build-dir", {});

    std::string targetName = optionValue(args, "--target", {});
    std::string targetId;
    for (const auto& target : project->targets()) {
        if (targetName.empty() || target.name == targetName || target.id == targetName) {
            targetId = target.id;
            break;
        }
    }
    if (!targetName.empty() && targetId.empty()) {
        std::cerr << "target not found: " << targetName << '\n';
        return 1;
    }
    auto graph = BuildController::buildGraphForProject(*project, targetId, configuration,
                                                       architecture, *selected,
                                                       request.buildDirectory);
    if (!graph) { std::cerr << graph.error() << '\n'; return 1; }

    std::cout << "Toolchain: " << selected->name << " (" << selected->version << ")\n";
    std::cout << "SDK:       " << (selected->sdk.empty() ? "<none>" : selected->sdk.string()) << '\n';
    BuildController controller(request);
    controller.graph() = *graph;
    controller.start([](const BuildEvent& event) {
        if (event.type == BuildEvent::Type::Command)
            std::cout << "  " << event.text << '\n';
        else if (event.type == BuildEvent::Type::Diagnostic)
            std::cerr << "  " << event.text << '\n';
    }, nullptr);
    auto result = controller.wait();
    for (const auto& diagnostic : result.diagnostics)
        std::cerr << diagnostic.file << ":" << diagnostic.line << ": " << diagnostic.severityName()
                  << ": " << diagnostic.message << '\n';
    std::cout << (result.success ? "Build succeeded" : clean ? "Clean complete" : "Build failed")
              << " — " << result.commandCount << " commands, " << result.skippedCount
              << " up to date, " << result.duration.count() << " ms\n";
    return result.success ? 0 : 1;
}

int commandToolchains(const std::vector<std::string>& args) {
    Settings settings;
    settings.load();
    auto registry = configuredRegistry(settings, optionValue(args, "--toolchain", {}));
    registry.detect();
    for (const auto& toolchain : registry.toolchains()) {
        std::cout << toolchain.name << " [" << toolchainKindName(toolchain.kind) << "]\n";
        std::cout << "  selected: " << (toolchain.name == registry.selectedName() ? "yes" : "no") << '\n';
        if (!toolchain.root.empty()) std::cout << "  root:     " << toolchain.root.string() << '\n';
        if (!toolchain.sdk.empty()) std::cout << "  SDK:      " << toolchain.sdk.string() << '\n';
        if (!toolchain.version.empty()) std::cout << "  version:  " << toolchain.version << '\n';
        std::cout << "  arches:   " << strings::join(toolchain.architectures, ", ") << '\n';
        for (const auto& [role, path] : toolchain.tools) {
            if (role.find(':') != std::string::npos) continue;
            std::cout << "  " << role << ": " << path.string() << '\n';
        }
        for (const auto& error : toolchain.errors) std::cout << "  error: " << error << '\n';
    }
    return 0;
}

int commandDoctor(const std::string& argument) {
    std::cout << "LXcode doctor\n";
    int failures = 0;
    for (const auto& tool : {"git", "cmake", "clang", "clang++"}) {
        const auto location = which(tool);
        std::cout << (location.empty() ? "MISSING " : "OK      ") << tool;
        if (!location.empty()) std::cout << " -> " << location;
        std::cout << '\n';
        if (location.empty()) ++failures;
    }
    Settings settings;
    auto loaded = settings.load();
    if (!loaded) { std::cerr << "settings: " << loaded.error() << '\n'; ++failures; }
    auto registry = configuredRegistry(settings);
    for (const auto& toolchain : registry.toolchains()) {
        for (const auto& issue : inspectToolchain(toolchain)) {
            std::cout << (issue.severity == Issue::Severity::Error ? "ERROR   " : "WARNING ")
                      << issue.category << ": " << issue.message << '\n';
            if (issue.severity == Issue::Severity::Error) ++failures;
        }
    }
    if (!argument.empty()) {
        auto path = resolveProject(argument);
        if (!path) { std::cerr << "ERROR   " << path.error() << '\n'; ++failures; }
        else {
            auto project = XcodeProject::open(*path);
            if (!project) { std::cerr << "ERROR   " << project.error() << '\n'; ++failures; }
            else {
                for (const auto& issue : inspectProject(*project)) {
                    std::cout << (issue.severity == Issue::Severity::Error ? "ERROR   " : "INFO    ")
                              << "Project: " << issue.message << '\n';
                }
            }
        }
    }
    std::cout << (failures == 0 ? "Diagnostics: no blocking issues\n"
                                : "Diagnostics: " + std::to_string(failures) + " blocking issue(s)\n");
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) return launchGui({});
    if (args[0] == "--help" || args[0] == "-h") { printHelp(); return 0; }
    if (args[0] == "--version" || args[0] == "-v") {
        std::cout << "LXcode 0.1.0 (independent open-source Xcode-compatible IDE)\n";
        std::cout << "Host: " << HOST_ARCHITECTURE << '\n';
        return 0;
    }
    if (args[0] == "open" && args.size() > 1) return launchGui(args[1]);
    if (args[0] == "info" && args.size() > 1) return commandInfo(args[1]);
    if (args[0] == "build" && args.size() > 1) return commandBuild(args[1], false, args);
    if (args[0] == "clean" && args.size() > 1) return commandBuild(args[1], true, args);
    if (args[0] == "toolchains") return commandToolchains(args);
    if (args[0] == "doctor") return commandDoctor(args.size() > 1 ? args[1] : std::string());
    if (args[0] == "create" && args.size() > 2) {
        std::filesystem::path parent(args[1]);
        auto project = XcodeProject::create(parent / (args[2] + ".xcodeproj"), args[2]);
        if (!project) { std::cerr << project.error() << '\n'; return 1; }
        std::cout << "Created " << project->path().string() << '\n';
        return 0;
    }
    std::cerr << "Unknown command: " << args[0] << "\n\n";
    printHelp();
    return 2;
}
