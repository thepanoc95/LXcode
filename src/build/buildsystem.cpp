#include "lxcode/build/buildsystem.hpp"

#include "lxcode/core/filesystem.hpp"
#include "lxcode/core/process.hpp"
#include "lxcode/core/strings.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <future>
#include <regex>
#include <set>
#include <sstream>
#include <thread>

namespace lxcode {

namespace {

std::string expandSetting(std::string value,
                          const std::map<std::string, std::string>& settings, int depth = 0) {
    if (depth > 8) return value;
    for (const auto& [key, replacement] : settings) {
        value = strings::replaceAll(value, "$(" + key + ")", replacement);
    }
    value = strings::replaceAll(value, "$(inherited)", "");
    for (int i = 0; i < 4; ++i) {
        const auto start = value.find("$(");
        if (start == std::string::npos) break;
        const auto end = value.find(')', start + 2);
        if (end == std::string::npos) break;
        value.replace(start, end + 1 - start, "");
    }
    (void)depth;
    return value;
}

std::vector<std::string> splitSetting(const std::string& value) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start < value.size()) {
        while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) ++start;
        if (start >= value.size()) break;
        if (value[start] == '"') {
            const auto end = value.find('"', start + 1);
            if (end == std::string::npos) { result.push_back(value.substr(start + 1)); break; }
            result.push_back(value.substr(start + 1, end - start - 1));
            start = end + 1;
        } else {
            const auto end = value.find_first_of(" \t", start);
            if (end == std::string::npos) { result.push_back(value.substr(start)); break; }
            result.push_back(value.substr(start, end - start));
            start = end;
        }
    }
    return result;
}

bool needsRebuild(const BuildCommand& command) {
    if (command.output.empty() || !fs::exists(command.output)) return true;
    std::error_code ec;
    const auto outputTime = std::filesystem::last_write_time(command.output, ec);
    if (ec) return true;
    for (const auto& input : command.inputs) {
        if (!fs::exists(input)) return true;
        const auto inputTime = std::filesystem::last_write_time(input, ec);
        if (ec || inputTime > outputTime) return true;
    }
    return false;
}

} // namespace

std::string Diagnostic::severityName() const {
    switch (severity) {
    case Severity::Note: return "note";
    case Severity::Warning: return "warning";
    case Severity::Error: return "error";
    }
    return "note";
}

std::vector<Diagnostic> parseDiagnostics(const std::string& output) {
    std::vector<Diagnostic> result;
    static const std::regex locationPattern(
        R"(^(.+?):(\d+):(?:(\d+):)?\s*(error|warning|note):\s*(.*)$)");
    static const std::regex simplePattern(R"(^(error|warning|note):\s*(.*)$)");
    static const std::regex linkerPattern(R"(^(?:ld|ld64|undefined|clang):\s*(.*)$)",
                                          std::regex::icase);
    std::smatch match;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        Diagnostic diagnostic;
        diagnostic.raw = line;
        if (std::regex_match(line, match, locationPattern)) {
            diagnostic.file = match[1];
            diagnostic.line = std::stoi(match[2]);
            diagnostic.column = match[3].matched ? std::stoi(match[3]) : 0;
            const auto severity = match[4].str();
            diagnostic.severity = severity == "error" ? Diagnostic::Severity::Error
                                  : severity == "warning" ? Diagnostic::Severity::Warning
                                                          : Diagnostic::Severity::Note;
            diagnostic.message = match[5];
        } else if (std::regex_match(line, match, simplePattern)) {
            const auto severity = match[1].str();
            diagnostic.severity = severity == "error" ? Diagnostic::Severity::Error
                                  : severity == "warning" ? Diagnostic::Severity::Warning
                                                          : Diagnostic::Severity::Note;
            diagnostic.message = match[2];
        } else if (std::regex_match(line, match, linkerPattern)) {
            diagnostic.severity = strings::toLower(line).find("error") != std::string::npos
                                      ? Diagnostic::Severity::Error
                                      : Diagnostic::Severity::Warning;
            diagnostic.message = match[1];
        } else {
            continue;
        }
        result.push_back(std::move(diagnostic));
    }
    std::sort(result.begin(), result.end(), [](const Diagnostic& a, const Diagnostic& b) {
        if (a.severity != b.severity) return a.severity > b.severity;
        return a.file < b.file;
    });
    return result;
}

void BuildGraph::add(BuildNode node) { nodes_.push_back(std::move(node)); }

Expected<std::vector<std::string>> BuildGraph::topologicalOrder() const {
    std::vector<std::string> order;
    std::set<std::string> visiting;
    std::set<std::string> visited;
    std::function<bool(const std::string&, std::string&)> visit =
        [&](const std::string& id, std::string& error) {
            if (visited.count(id)) return true;
            if (visiting.count(id)) {
                error = "dependency cycle involving " + id;
                return false;
            }
            visiting.insert(id);
            for (const auto& node : nodes_) {
                if (node.id != id) continue;
                for (const auto& dependency : node.dependencies) {
                    if (!visit(dependency, error)) return false;
                }
                order.push_back(id);
                break;
            }
            visiting.erase(id);
            visited.insert(id);
            return true;
        };
    for (const auto& node : nodes_) {
        std::string error;
        if (!visit(node.id, error)) return Expected<std::vector<std::string>>::failure(error);
    }
    return order;
}

std::vector<std::string> BuildGraph::validate() const {
    std::vector<std::string> issues;
    std::set<std::string> ids;
    for (const auto& node : nodes_) {
        if (!ids.insert(node.id).second) issues.push_back("duplicate build node: " + node.id);
    }
    for (const auto& node : nodes_) {
        for (const auto& dependency : node.dependencies) {
            if (!ids.count(dependency))
                issues.push_back("node " + node.id + " depends on missing node " + dependency);
        }
        for (const auto& command : node.commands) {
            if (command.kind != CommandKind::Copy && command.executable.empty())
                issues.push_back("command " + command.description + " has no executable");
        }
    }
    return issues;
}

Expected<BuildGraph> BuildController::buildGraphForProject(
    const XcodeProject& project, const std::string& targetId, const std::string& configuration,
    const std::string& architecture, const Toolchain& toolchain,
    const std::filesystem::path& buildDirectory) {
    if (!toolchain.valid() && toolchain.kind == ToolchainKind::OSXCross)
        return Expected<BuildGraph>::failure("toolchain is invalid: " +
                                             (toolchain.errors.empty() ? "unknown error"
                                                                       : toolchain.errors.front()));
    auto targets = project.targets();
    if (targets.empty()) return Expected<BuildGraph>::failure("project has no targets");
    if (!targetId.empty()) {
        const auto selected = std::find_if(targets.begin(), targets.end(),
                                           [&](const PbxTargetInfo& t) { return t.id == targetId; });
        if (selected == targets.end())
            return Expected<BuildGraph>::failure("target not found: " + targetId);
        targets = {*selected};
    }

    auto outputRoot = buildDirectory;
    if (outputRoot.empty())
        outputRoot = project.directory() / "build" / configuration / architecture;

    BuildGraph graph;
    for (const auto& target : targets) {
        BuildNode node;
        node.id = target.id;
        node.name = target.name;
        for (const auto& dependency : target.dependencies) {
            const auto proxyId = project.document().fieldString(dependency, "container");
            const auto remote = project.document().fieldString(proxyId, "remoteGlobalIDString");
            if (!remote.empty()) node.dependencies.push_back(remote);
            else if (!proxyId.empty()) node.dependencies.push_back(proxyId);
        }

        const auto settings = project.buildSettings(target.id, configuration);
        const auto expanded = [&](const std::string& key) {
            return expandSetting(settings.count(key) ? settings.at(key) : std::string(), settings);
        };
        const auto deployment = expanded("MACOSX_DEPLOYMENT_TARGET");
        osxcross::CompileOptions compileOptions;
        compileOptions.architecture = architecture;
        compileOptions.deploymentTarget = deployment.empty() ? "11.0" : deployment;
        compileOptions.standard = expanded("CLANG_CXX_LANGUAGE_STANDARD");
        if (compileOptions.standard.empty()) compileOptions.standard = "c++17";
        for (const auto& path : splitSetting(expanded("HEADER_SEARCH_PATHS")))
            compileOptions.includeDirectories.push_back(path);
        for (const auto& path : splitSetting(expanded("USER_HEADER_SEARCH_PATHS")))
            compileOptions.includeDirectories.push_back(path);
        for (const auto& define : splitSetting(expanded("GCC_PREPROCESSOR_DEFINITIONS")))
            if (define != "$(inherited)") compileOptions.defines.push_back(define);
        for (const auto& flag : splitSetting(expanded("OTHER_CFLAGS"))) compileOptions.flags.push_back(flag);
        for (const auto& flag : splitSetting(expanded("OTHER_CPLUSPLUSFLAGS"))) compileOptions.flags.push_back(flag);
        compileOptions.flags.push_back("-Wall");

        osxcross::LinkOptions linkOptions;
        linkOptions.architecture = architecture;
        linkOptions.deploymentTarget = compileOptions.deploymentTarget;
        for (const auto& path : splitSetting(expanded("FRAMEWORK_SEARCH_PATHS")))
            linkOptions.frameworkDirectories.push_back(path);
        for (const auto& path : splitSetting(expanded("LIBRARY_SEARCH_PATHS")))
            linkOptions.libraryDirectories.push_back(path);
        auto otherLdFlags = splitSetting(expanded("OTHER_LDFLAGS"));
        for (std::size_t i = 0; i < otherLdFlags.size(); ++i) {
            if (otherLdFlags[i] == "-framework" && i + 1 < otherLdFlags.size()) {
                linkOptions.frameworks.push_back(otherLdFlags[++i]);
            } else if (strings::startsWith(otherLdFlags[i], "-l")) {
                linkOptions.libraries.push_back(otherLdFlags[i].substr(2));
            } else {
                linkOptions.flags.push_back(otherLdFlags[i]);
            }
        }

        std::vector<std::filesystem::path> objects;
        for (const auto& phase : target.phases) {
            if (phase.isa == "PBXSourcesBuildPhase") {
                for (const auto& fileRef : phase.fileRefs) {
                    const auto source = project.resolvedPath(fileRef);
                    if (source.empty()) continue;
                    const auto object = outputRoot / target.id / (source.filename().string() + ".o");
                    std::error_code ec;
                    std::filesystem::create_directories(object.parent_path(), ec);
                    const auto args = osxcross::compileArguments(toolchain, source, object,
                                                                 compileOptions);
                    if (args.empty()) {
                        return Expected<BuildGraph>::failure(
                            "no compiler wrapper available for " + architecture);
                    }
                    const auto compiler = osxcross::toolFor(toolchain, architecture, "clang++");
                    BuildCommand command;
                    command.kind = CommandKind::Compile;
                    command.description = "Compile " + source.filename().string();
                    command.executable = compiler.string();
                    command.arguments = args;
                    command.workingDirectory = project.directory();
                    command.output = object;
                    command.inputs = {source};
                    node.commands.push_back(std::move(command));
                    objects.push_back(object);
                }
            } else if (phase.isa == "PBXShellScriptBuildPhase") {
                BuildCommand command;
                command.kind = CommandKind::Script;
                command.description = "Run script for " + target.name;
                command.executable = "/bin/sh";
                command.arguments = {"-c", phase.shellScript};
                command.workingDirectory = project.directory();
                node.commands.push_back(std::move(command));
            } else if (phase.isa == "PBXResourcesBuildPhase") {
                for (const auto& fileRef : phase.fileRefs) {
                    const auto source = project.resolvedPath(fileRef);
                    if (source.empty()) continue;
                    BuildCommand command;
                    command.kind = CommandKind::Copy;
                    command.description = "Copy " + source.filename().string();
                    command.output = outputRoot / source.filename();
                    command.inputs = {source};
                    node.commands.push_back(std::move(command));
                }
            }
        }

        if (!objects.empty() && !target.productReference.empty()) {
            auto productName = project.document().fieldString(target.productReference, "path");
            if (productName.empty()) productName = target.productName;
            const auto product = outputRoot / productName;
            const auto args = osxcross::linkArguments(toolchain, objects, product, linkOptions);
            if (!args.empty()) {
                BuildCommand command;
                command.kind = CommandKind::Link;
                command.description = "Link " + productName;
                command.executable = osxcross::toolFor(toolchain, architecture, "clang++").string();
                command.arguments = args;
                command.workingDirectory = project.directory();
                command.output = product;
                command.inputs = objects;
                node.commands.push_back(std::move(command));
            }
        }
        graph.add(std::move(node));
    }
    return graph;
}

struct BuildController::Impl {
    std::thread thread;
    std::atomic_bool running{false};
    std::atomic_bool cancelled{false};
    BuildResult lastResult;
};

BuildController::BuildController(BuildRequest request)
    : impl_(std::make_unique<Impl>()), request_(std::move(request)) {}

BuildController::~BuildController() { cancel(); }

void BuildController::start(std::function<void(const BuildEvent&)> onEvent,
                            std::function<void(const BuildResult&)> onFinished) {
    if (impl_->running.load()) return;
    impl_->cancelled.store(false);
    impl_->running.store(true);
    impl_->thread = std::thread([this, onEvent, onFinished]() {
        BuildResult result;
        const auto started = std::chrono::steady_clock::now();
        auto emit = [&](BuildEvent::Type type, std::string text, int index = 0, int count = 0) {
            if (!onEvent) return;
            BuildEvent event;
            event.type = type;
            event.text = std::move(text);
            event.commandIndex = index;
            event.commandCount = count;
            onEvent(event);
        };

        emit(BuildEvent::Type::Started, "Build started: " + request_.configuration + " · " +
                                            request_.architecture);

        if (request_.clean) {
            std::error_code ec;
            if (fs::exists(request_.buildDirectory))
                std::filesystem::remove_all(request_.buildDirectory, ec);
            emit(BuildEvent::Type::Output, "Cleaned " + request_.buildDirectory.string());
        }

        auto issues = graph_.validate();
        if (!issues.empty()) {
            Diagnostic diagnostic;
            diagnostic.severity = Diagnostic::Severity::Error;
            diagnostic.message = issues.front();
            diagnostic.raw = issues.front();
            result.diagnostics.push_back(diagnostic);
            emit(BuildEvent::Type::Diagnostic, issues.front());
        }

        const auto order = graph_.topologicalOrder();
        if (!order) {
            Diagnostic diagnostic;
            diagnostic.severity = Diagnostic::Severity::Error;
            diagnostic.message = order.error();
            diagnostic.raw = order.error();
            result.diagnostics.push_back(diagnostic);
            emit(BuildEvent::Type::Diagnostic, order.error());
        } else {
            const auto nodes = graph_.nodes();
            int executed = 0;
            int total = 0;
            for (const auto& node : nodes) total += static_cast<int>(node.commands.size());
            for (const auto& id : *order) {
                if (impl_->cancelled.load()) break;
                const auto it = std::find_if(nodes.begin(), nodes.end(),
                                             [&](const BuildNode& n) { return n.id == id; });
                if (it == nodes.end()) continue;
                for (const auto& command : it->commands) {
                    if (impl_->cancelled.load()) break;
                    ++executed;
                    if (!needsRebuild(command)) {
                        ++result.skippedCount;
                        emit(BuildEvent::Type::Output, "Up to date: " + command.description,
                             executed, total);
                        continue;
                    }
                    emit(BuildEvent::Type::Command, command.description, executed, total);
                    result.log += "$ " + command.description + "\n";

                    if (command.kind == CommandKind::Copy) {
                        std::error_code ec;
                        std::filesystem::create_directories(command.output.parent_path(), ec);
                        std::filesystem::copy_file(command.inputs.front(), command.output,
                                                   std::filesystem::copy_options::overwrite_existing, ec);
                        if (ec) {
                            Diagnostic diagnostic;
                            diagnostic.severity = Diagnostic::Severity::Error;
                            diagnostic.message = ec.message();
                            diagnostic.raw = ec.message();
                            result.diagnostics.push_back(diagnostic);
                        }
                        continue;
                    }

                    ProcessOptions options;
                    options.workingDirectory = command.workingDirectory.string();
                    options.environment = {};
                    for (const auto& variable : request_.environmentVariables) {
                        const auto equals = variable.find('=');
                        if (equals != std::string::npos)
                            options.environment[variable.substr(0, equals)] =
                                variable.substr(equals + 1);
                    }
                    options.cancelFlag = &impl_->cancelled;
                    options.onOutput = [&](const std::string& chunk, bool isStderr) {
                        (void)isStderr;
                        result.log.append(chunk);
                        emit(BuildEvent::Type::Output, chunk, executed, total);
                    };
                    auto processResult = runProcess(command.executable, command.arguments, options);
                    for (auto& diagnostic : parseDiagnostics(processResult.stdoutText)) {
                        result.diagnostics.push_back(diagnostic);
                        BuildEvent event;
                        event.type = BuildEvent::Type::Diagnostic;
                        event.diagnostic = diagnostic;
                        event.text = diagnostic.raw;
                        if (onEvent) onEvent(event);
                    }
                    ++result.commandCount;
                    if (processResult.cancelled || impl_->cancelled.load()) {
                        result.cancelled = true;
                        emit(BuildEvent::Type::Cancelled, "Build cancelled", executed, total);
                        break;
                    }
                    if (processResult.exitCode != 0) {
                        Diagnostic diagnostic;
                        diagnostic.severity = Diagnostic::Severity::Error;
                        diagnostic.message = command.description + " failed with exit code " +
                                             std::to_string(processResult.exitCode);
                        diagnostic.raw = diagnostic.message;
                        result.diagnostics.push_back(diagnostic);
                        BuildEvent event;
                        event.type = BuildEvent::Type::Diagnostic;
                        event.diagnostic = diagnostic;
                        event.text = diagnostic.raw;
                        if (onEvent) onEvent(event);
                        break;
                    }
                }
            }
        }

        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        result.success = !result.cancelled && result.diagnostics.empty() &&
                         std::none_of(result.diagnostics.begin(), result.diagnostics.end(),
                                      [](const Diagnostic& d) {
                                          return d.severity == Diagnostic::Severity::Error;
                                      });
        emit(result.cancelled ? BuildEvent::Type::Cancelled : BuildEvent::Type::Finished,
             result.success ? "Build succeeded" : "Build failed");
        impl_->lastResult = result;
        if (onFinished) onFinished(result);
        impl_->running.store(false);
    });
}

bool BuildController::running() const { return impl_->running.load(); }

void BuildController::cancel() {
    impl_->cancelled.store(true);
    if (impl_->thread.joinable()) impl_->thread.join();
}

BuildResult BuildController::wait() {
    if (impl_->thread.joinable()) impl_->thread.join();
    return impl_->lastResult;
}

Expected<std::filesystem::path> generateOsxcrossToolchainFile(
    const Toolchain& toolchain, const std::string& architecture,
    const std::filesystem::path& outputPath) {
    if (toolchain.kind != ToolchainKind::OSXCross)
        return Expected<std::filesystem::path>::failure("requires an OSXCross toolchain");
    const auto cc = osxcross::toolFor(toolchain, architecture, "clang");
    const auto cxx = osxcross::toolFor(toolchain, architecture, "clang++");
    if (cc.empty() || cxx.empty())
        return Expected<std::filesystem::path>::failure("clang wrappers are missing");
    std::string contents;
    contents += "set(CMAKE_SYSTEM_NAME Darwin)\n";
    contents += "set(CMAKE_SYSTEM_PROCESSOR " + architecture + ")\n";
    contents += "set(CMAKE_OSX_ARCHITECTURES " + architecture + " CACHE STRING \"\" FORCE)\n";
    if (!toolchain.sdk.empty())
        contents += "set(CMAKE_OSX_SYSROOT \"" + toolchain.sdk.string() + "\" CACHE PATH \"\" FORCE)\n";
    contents += "set(CMAKE_C_COMPILER \"" + cc.string() + "\")\n";
    contents += "set(CMAKE_CXX_COMPILER \"" + cxx.string() + "\")\n";
    if (const auto* ar = toolchain.tool("ar"))
        contents += "set(CMAKE_AR \"" + ar->string() + "\")\n";
    if (const auto* ranlib = toolchain.tool("ranlib"))
        contents += "set(CMAKE_RANLIB \"" + ranlib->string() + "\")\n";
    contents += "set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)\n";
    contents += "set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)\n";
    contents += "set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)\n";
    auto saved = fs::writeTextAtomic(outputPath, contents, false);
    if (!saved) return Expected<std::filesystem::path>::failure(saved.error());
    return outputPath;
}

} // namespace lxcode
