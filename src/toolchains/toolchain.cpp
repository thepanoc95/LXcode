#include "lxcode/toolchains/toolchain.hpp"

#include "lxcode/core/filesystem.hpp"
#include "lxcode/core/process.hpp"
#include "lxcode/core/strings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <regex>
#include <set>

namespace lxcode {

namespace {

bool endsWithRole(const std::string& name, const std::string& role) {
    return strings::endsWith(name, "-" + role) || name == role;
}

std::string versionFromSdkName(const std::string& name) {
    std::smatch match;
    if (std::regex_search(name, match, std::regex("([0-9]+\\.[0-9]+)"))) return match[1];
    return {};
}

std::string normalizeArchitecture(std::string value) {
    if (value == "aarch64") return "arm64";
    if (value == "amd64" || value == "x86-64") return "x86_64";
    return value;
}

bool versionNewer(const std::filesystem::path& a, const std::filesystem::path& b) {
    const auto va = versionFromSdkName(a.stem().string());
    const auto vb = versionFromSdkName(b.stem().string());
    try {
        return std::stod(va) > std::stod(vb);
    } catch (...) {
        return a.string() > b.string();
    }
}

} // namespace

bool Toolchain::supports(const std::string& architecture) const {
    return std::find(architectures.begin(), architectures.end(), architecture) != architectures.end();
}

const std::filesystem::path* Toolchain::tool(const std::string& role) const {
    const auto it = tools.find(role);
    return it == tools.end() ? nullptr : &it->second;
}

namespace osxcross {

std::vector<std::filesystem::path> candidateRoots() {
    std::vector<std::filesystem::path> roots;
    if (const char* env = std::getenv("OSXCROSS_ROOT"))
        if (*env) roots.emplace_back(env);
    for (const char* candidate : {"/opt/osxcross", "/usr/local/osxcross", "/usr/lib/osxcross",
                                  "~/osxcross", "~/.local/osxcross"}) {
        std::string path = candidate;
        if (path.rfind("~", 0) == 0) {
            if (const char* home = std::getenv("HOME")) path = std::string(home) + path.substr(1);
            else continue;
        }
        roots.emplace_back(path);
    }
    return roots;
}

Expected<Toolchain> detect(const std::filesystem::path& root) {
    Toolchain toolchain;
    toolchain.kind = ToolchainKind::OSXCross;
    toolchain.root = root;
    toolchain.name = "OSXCross (" + root.filename().string() + ")";
    std::error_code ec;
    const auto bin = root / "bin";
    if (!std::filesystem::is_directory(bin, ec)) {
        toolchain.errors.push_back("missing bin directory: " + bin.string());
        return toolchain;
    }

    static const std::vector<std::string> roles = {"clang++", "clang", "ld64.lld", "ld.lld",
                                                   "ld64", "ld", "lipo", "otool", "ar", "nm",
                                                   "ranlib"};
    std::set<std::string> triples;
    for (std::filesystem::directory_iterator it(bin, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec) && !it->is_symlink(ec)) continue;
        const auto name = it->path().filename().string();
        for (const auto& role : roles) {
            if (!endsWithRole(name, role)) continue;
            if (role == "clang" && strings::endsWith(name, "clang++")) continue;
            if (role == "ld" && strings::endsWith(name, "ld64")) continue;
            if (toolchain.tools.find(role) == toolchain.tools.end() || name.find('-') == std::string::npos)
                toolchain.tools[role] = it->path();
            if (strings::endsWith(name, "-" + role)) {
                auto triple = name.substr(0, name.size() - role.size() - 1);
                triples.insert(triple);
                toolchain.tools[role + ":" + triple] = it->path();
                const auto apple = triple.find("-apple-");
                if (apple != std::string::npos)
                    toolchain.architectures.push_back(normalizeArchitecture(triple.substr(0, apple)));
            }
        }
    }
    std::sort(toolchain.architectures.begin(), toolchain.architectures.end());
    toolchain.architectures.erase(std::unique(toolchain.architectures.begin(),
                                             toolchain.architectures.end()),
                                  toolchain.architectures.end());

    const auto sdkRoot = root / "SDK";
    if (std::filesystem::is_directory(sdkRoot, ec)) {
        for (std::filesystem::directory_iterator it(sdkRoot, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            if (it->is_directory(ec) && strings::endsWith(it->path().filename().string(), ".sdk"))
                toolchain.sdks.push_back(it->path());
        }
        std::sort(toolchain.sdks.begin(), toolchain.sdks.end(), versionNewer);
    }
    if (!toolchain.sdks.empty()) toolchain.sdk = toolchain.sdks.front();
    else toolchain.errors.push_back("no macOS SDK found under " + sdkRoot.string());

    if (toolchain.tools.find("clang") == toolchain.tools.end() &&
        toolchain.tools.find("clang++") == toolchain.tools.end())
        toolchain.errors.push_back("no clang wrapper found in " + bin.string());
    if (toolchain.architectures.empty())
        toolchain.warnings.push_back("no architecture-specific wrappers found; using generic wrappers");

    const auto conf = bin / "osxcross-conf";
    if (std::filesystem::exists(conf, ec)) {
        auto result = runProcess(conf.string(), {"--version"}, {{}, {}, std::chrono::seconds(3)});
        if (result.exitCode == 0) {
            const auto line = result.stdoutText.find_first_not_of(" \t\r\n");
            if (line != std::string::npos) {
                const auto end = result.stdoutText.find('\n', line);
                toolchain.version = strings::trim(result.stdoutText.substr(
                    line, end == std::string::npos ? std::string::npos : end - line));
            }
        }
    }
    if (toolchain.version.empty() && !toolchain.sdks.empty())
        toolchain.version = "SDK " + toolchain.sdk.stem().string();
    return toolchain;
}

std::filesystem::path toolFor(const Toolchain& toolchain, const std::string& architecture,
                              const std::string& role) {
    if (!architecture.empty()) {
        const std::string exact = role + ":" + architecture;
        if (const auto* path = toolchain.tool(exact)) return *path;
        for (const auto& [key, path] : toolchain.tools) {
            if (!strings::startsWith(key, role + ":")) continue;
            if (strings::startsWith(key.substr(role.size() + 1), architecture + "-")) return path;
        }
    }
    if (const auto* path = toolchain.tool(role)) return *path;

    // Fall back to any clang wrapper when clang++ is unavailable; the driver
    // still compiles C++ when given -x c++ and the standard library is present.
    if (role == "clang++") return toolFor(toolchain, architecture, "clang");
    return {};
}

std::vector<std::string> compileArguments(const Toolchain& toolchain,
                                          const std::filesystem::path& source,
                                          const std::filesystem::path& output,
                                          const CompileOptions& options) {
    const auto compiler = toolFor(toolchain, options.architecture, "clang++");
    if (compiler.empty()) return {};
    std::vector<std::string> args;
    if (!toolchain.sdk.empty()) {
        args.push_back("-isysroot");
        args.push_back(toolchain.sdk.string());
    }
    if (!options.architecture.empty()) {
        args.push_back("-arch");
        args.push_back(options.architecture);
    }
    if (!options.deploymentTarget.empty())
        args.push_back("-mmacosx-version-min=" + options.deploymentTarget);
    if (!options.standard.empty()) args.push_back("-std=" + options.standard);
    for (const auto& include : options.includeDirectories) args.push_back("-I" + include);
    for (const auto& define : options.defines) args.push_back("-D" + define);
    args.insert(args.end(), options.flags.begin(), options.flags.end());
    args.push_back("-c");
    args.push_back(source.string());
    args.push_back("-o");
    args.push_back(output.string());
    return args;
}

std::vector<std::string> linkArguments(const Toolchain& toolchain,
                                       const std::vector<std::filesystem::path>& objects,
                                       const std::filesystem::path& output,
                                       const LinkOptions& options) {
    const auto linker = toolFor(toolchain, options.architecture, "clang++");
    if (linker.empty()) return {};
    std::vector<std::string> args;
    if (!toolchain.sdk.empty()) {
        args.push_back("-isysroot");
        args.push_back(toolchain.sdk.string());
    }
    if (!options.architecture.empty()) {
        args.push_back("-arch");
        args.push_back(options.architecture);
    }
    if (!options.deploymentTarget.empty())
        args.push_back("-mmacosx-version-min=" + options.deploymentTarget);
    for (const auto& dir : options.frameworkDirectories) {
        args.push_back("-F" + dir);
    }
    for (const auto& dir : options.libraryDirectories) args.push_back("-L" + dir);
    for (const auto& framework : options.frameworks) {
        args.push_back("-framework");
        args.push_back(framework);
    }
    for (const auto& object : objects) args.push_back(object.string());
    for (const auto& library : options.libraries) args.push_back("-l" + library);
    args.insert(args.end(), options.flags.begin(), options.flags.end());
    args.push_back("-o");
    args.push_back(output.string());
    return args;
}

std::vector<std::string> lipoArguments(const Toolchain& toolchain,
                                       const std::vector<std::filesystem::path>& slices,
                                       const std::filesystem::path& output) {
    auto lipo = toolchain.tool("lipo");
    if (!lipo && !toolchain.root.empty()) {
        std::error_code ec;
        for (std::filesystem::directory_iterator it(toolchain.root / "bin", ec), end;
             it != end && !ec; it.increment(ec)) {
            if (it->path().filename().string().find("lipo") != std::string::npos) {
                lipo = &it->path();
                break;
            }
        }
    }
    (void)lipo;
    std::vector<std::string> args = {"-create", "-output", output.string()};
    for (const auto& slice : slices) args.push_back(slice.string());
    return args;
}

} // namespace osxcross

ToolchainRegistry& ToolchainRegistry::add(Toolchain toolchain) {
    const auto existing = std::find_if(toolchains_.begin(), toolchains_.end(),
                                       [&](const Toolchain& item) {
                                           return item.root == toolchain.root &&
                                                  item.kind == toolchain.kind;
                                       });
    if (existing != toolchains_.end()) *existing = std::move(toolchain);
    else toolchains_.push_back(std::move(toolchain));
    return *this;
}

ToolchainRegistry& ToolchainRegistry::addPath(const std::filesystem::path& root) {
    extraRoots_.push_back(root);
    return *this;
}

std::vector<Toolchain> ToolchainRegistry::detect() {
    std::vector<std::filesystem::path> roots = extraRoots_;
    const auto candidates = osxcross::candidateRoots();
    std::set<std::string> candidateNames;
    for (const auto& candidate : candidates) {
        candidateNames.insert(candidate.lexically_normal().string());
        roots.push_back(candidate);
    }
    std::set<std::string> seen;
    for (const auto& root : roots) {
        auto normalized = root.lexically_normal();
        if (!seen.insert(normalized.string()).second) continue;
        // Automatically probed locations should stay quiet when absent. Only
        // explicitly configured roots are reported as broken toolchains.
        if (candidateNames.count(normalized.string()) && !fs::exists(normalized)) continue;
        auto toolchain = osxcross::detect(normalized);
        if (toolchain) add(*toolchain);
    }

    Toolchain system;
    system.kind = ToolchainKind::System;
    system.name = "System compiler";
    for (const auto& role : {"clang", "clang++", "ar", "nm"}) {
        auto path = which(role);
        if (!path.empty()) system.tools[role] = path;
    }
    system.architectures = {std::string(HOST_ARCHITECTURE)};
    system.warnings.push_back("No Apple SDK configured; system builds cannot target macOS");
    add(std::move(system));

    if (selected_.empty()) {
        for (const auto& toolchain : toolchains_) {
            if (toolchain.kind == ToolchainKind::OSXCross && toolchain.valid()) {
                selected_ = toolchain.name;
                break;
            }
        }
    }
    if (selected_.empty()) {
        for (const auto& toolchain : toolchains_) {
            if (toolchain.kind == ToolchainKind::System) {
                selected_ = toolchain.name;
                break;
            }
        }
    }
    return toolchains_;
}

Toolchain* ToolchainRegistry::find(const std::string& name) {
    for (auto& toolchain : toolchains_)
        if (toolchain.name == name) return &toolchain;
    return nullptr;
}

const Toolchain* ToolchainRegistry::find(const std::string& name) const {
    for (const auto& toolchain : toolchains_)
        if (toolchain.name == name) return &toolchain;
    return nullptr;
}

Expected<Toolchain> ToolchainRegistry::selected() const {
    if (selected_.empty()) return Expected<Toolchain>::failure("no toolchain selected");
    const auto* toolchain = find(selected_);
    if (!toolchain) return Expected<Toolchain>::failure("selected toolchain not found: " + selected_);
    return *toolchain;
}

void ToolchainRegistry::setSelected(std::string name) { selected_ = std::move(name); }

Expected<void> ToolchainRegistry::loadConfiguration(const std::filesystem::path& path) {
    auto text = fs::readText(path);
    if (!text) return Expected<void>::failure(text.error());
    try {
        auto json = nlohmann::json::parse(*text);
        selected_ = json.value("selected", "");
        if (json.contains("toolchains")) {
            for (const auto& item : json["toolchains"]) {
                Toolchain toolchain;
                toolchain.name = item.value("name", "OSXCross");
                toolchain.root = item.value("root", "");
                if (item.contains("sdk")) toolchain.sdk = item.value("sdk", "");
                toolchain.architectures = item.value("architectures", std::vector<std::string>{});
                toolchain.kind = ToolchainKind::OSXCross;
                add(std::move(toolchain));
            }
        }
        for (const auto& root : json.value("roots", std::vector<std::string>{}))
            extraRoots_.emplace_back(root);
    } catch (const std::exception& error) {
        return Expected<void>::failure(std::string("invalid toolchain configuration: ") + error.what());
    }
    return {};
}

Expected<void> ToolchainRegistry::saveConfiguration(const std::filesystem::path& path) const {
    nlohmann::json json;
    json["selected"] = selected_;
    json["roots"] = {};
    for (const auto& root : extraRoots_) json["roots"].push_back(root.string());
    json["toolchains"] = {};
    for (const auto& toolchain : toolchains_) {
        if (toolchain.kind != ToolchainKind::OSXCross) continue;
        nlohmann::json item;
        item["name"] = toolchain.name;
        item["root"] = toolchain.root.string();
        item["sdk"] = toolchain.sdk.string();
        item["architectures"] = toolchain.architectures;
        json["toolchains"].push_back(item);
    }
    return fs::writeTextAtomic(path, json.dump(2) + "\n");
}

std::string toolchainKindName(ToolchainKind kind) {
    switch (kind) {
    case ToolchainKind::OSXCross: return "OSXCross";
    case ToolchainKind::System: return "System";
    case ToolchainKind::CMake: return "CMake";
    }
    return "Unknown";
}

} // namespace lxcode
