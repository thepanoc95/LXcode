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
    if (role == "clang++") return osxcross::toolFor(toolchain, architecture, "clang");
    return {};
}

std::vector<std::string> compileArguments(const Toolchain& toolchain,
                                          const std::filesystem::path& source,
                                          const std::filesystem::path& output,
                                          const CompileOptions& options) {
    const auto compiler = osxcross::toolFor(toolchain, options.architecture, "clang++");
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
    const auto linker = osxcross::toolFor(toolchain, options.architecture, "clang++");
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

std::filesystem::path toolFor(const Toolchain& toolchain, const std::string& architecture,
                              const std::string& role) {
    return osxcross::toolFor(toolchain, architecture, role);
}

namespace customsdk {

namespace {

std::string tripleFor(const std::string& architecture) {
    return (architecture.empty() ? std::string("arm64") : architecture) + "-apple-darwin";
}

std::string compilerWrapper(const std::string& executable, const std::string& target) {
    std::string script = "#!/bin/sh\n";
    script += "# Generated by LXcode for a custom macOS SDK bundle. Do not edit.\n";
    script += "exec \"" + executable + "\"";
    if (!target.empty()) script += " --target=" + target;
    script += " \"$@\"\n";
    return script;
}

std::string toolWrapper(const std::string& executable) {
    return "#!/bin/sh\n# Generated by LXcode. Do not edit.\nexec \"" + executable + "\" \"$@\"\n";
}

Expected<void> writeScript(const std::filesystem::path& path, const std::string& contents) {
    auto saved = fs::writeTextAtomic(path, contents, false);
    if (!saved) return saved;
    std::error_code ec;
    std::filesystem::permissions(path, std::filesystem::perms::owner_exec |
                                          std::filesystem::perms::group_exec |
                                          std::filesystem::perms::others_exec,
                                  std::filesystem::perm_options::add, ec);
    if (ec) return Expected<void>::failure("cannot make wrapper executable: " + ec.message());
    return {};
}

std::string resolveCompiler(std::string configured, const char* fallback) {
    if (!configured.empty() && fs::exists(configured)) return configured;
    const auto discovered = which(fallback);
    return discovered.empty() ? configured : discovered;
}

} // namespace

bool isBundle(const std::filesystem::path& root) {
    return fs::exists(root / manifestFileName);
}

Expected<Manifest> readManifest(const std::filesystem::path& root) {
    const auto path = root / manifestFileName;
    auto text = fs::readText(path);
    if (!text) return Expected<Manifest>::failure("cannot read SDK bundle manifest: " + path.string());
    try {
        const auto json = nlohmann::json::parse(*text);
        if (json.value("format", std::string("lxcode-sdk-bundle")) != "lxcode-sdk-bundle")
            return Expected<Manifest>::failure("not an LXcode SDK bundle manifest: " + path.string());
        Manifest manifest;
        manifest.version = json.value("version", 1);
        if (manifest.version != 1)
            return Expected<Manifest>::failure("unsupported SDK bundle manifest version: " +
                                               std::to_string(manifest.version));
        manifest.name = json.value("name", root.filename().string());
        manifest.compiler = json.value("compiler", "");
        manifest.compilerCXX = json.value("compilerCXX", "");
        manifest.linker = json.value("linker", "lld");
        manifest.sdkRoot = json.value("sdkRoot", "");
        manifest.deploymentTarget = json.value("deploymentTarget", "11.0");
        if (json.contains("architectures") && json["architectures"].is_array())
            manifest.architectures = json["architectures"].get<std::vector<std::string>>();
        return manifest;
    } catch (const std::exception& error) {
        return Expected<Manifest>::failure(std::string("invalid SDK bundle manifest: ") + error.what());
    }
}

Expected<void> writeManifest(const std::filesystem::path& root, const Manifest& manifest) {
    nlohmann::json json;
    json["format"] = "lxcode-sdk-bundle";
    json["version"] = 1;
    json["name"] = manifest.name.empty() ? root.filename().string() : manifest.name;
    json["compiler"] = manifest.compiler;
    json["compilerCXX"] = manifest.compilerCXX;
    json["linker"] = manifest.linker.empty() ? "lld" : manifest.linker;
    json["sdkRoot"] = manifest.sdkRoot.string();
    json["architectures"] = manifest.architectures;
    json["deploymentTarget"] = manifest.deploymentTarget;
    auto saved = fs::writeTextAtomic(root / manifestFileName, json.dump(2) + "\n", false);
    if (!saved) return saved;
    return {};
}

Expected<void> writeWrappers(const std::filesystem::path& root, const Manifest& manifest) {
    const auto bin = root / "bin";
    std::error_code ec;
    std::filesystem::create_directories(bin, ec);
    if (ec) return Expected<void>::failure("cannot create wrapper directory: " + ec.message());

    const auto compiler = resolveCompiler(manifest.compiler, "clang");
    const auto compilerCXX = resolveCompiler(manifest.compilerCXX, "clang++");
    if (compiler.empty() || compilerCXX.empty())
        return Expected<void>::failure("clang and clang++ are required to create an SDK bundle");

    auto saved = writeScript(bin / "clang", compilerWrapper(compiler, {}));
    if (!saved) return saved;
    saved = writeScript(bin / "clang++", compilerWrapper(compilerCXX, {}));
    if (!saved) return saved;

    for (const auto& architecture : manifest.architectures) {
        const auto target = tripleFor(architecture);
        saved = writeScript(bin / (architecture + "-apple-darwin-clang"),
                            compilerWrapper(compiler, target));
        if (!saved) return saved;
        saved = writeScript(bin / (architecture + "-apple-darwin-clang++"),
                            compilerWrapper(compilerCXX, target));
        if (!saved) return saved;
    }

    for (const char* tool : {"ar", "nm", "ranlib"}) {
        const auto location = which(tool);
        if (location.empty()) continue;
        saved = writeScript(bin / tool, toolWrapper(location));
        if (!saved) return saved;
    }
    saved = writeScript(bin / "lipo",
                        "#!/bin/sh\n"
                        "# Generated by LXcode. Universal binaries need a real lipo in this bundle.\n"
                        "echo 'lxcode: no lipo is installed in this SDK bundle' >&2\n"
                        "exit 2\n");
    if (!saved) return saved;
    return {};
}

Expected<std::filesystem::path> createBundle(const std::filesystem::path& root,
                                             const Manifest& manifest) {
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec) return Expected<std::filesystem::path>::failure("cannot create bundle directory: " +
                                                            ec.message());
    std::filesystem::create_directories(root / "SDK", ec);
    if (ec) return Expected<std::filesystem::path>::failure("cannot create SDK directory: " + ec.message());

    auto written = writeManifest(root, manifest);
    if (!written) return Expected<std::filesystem::path>::failure(written.error());
    written = writeWrappers(root, manifest);
    if (!written) return Expected<std::filesystem::path>::failure(written.error());

    const auto readme =
        "# LXcode macOS SDK bundle\n\n"
        "This bundle provides Linux-side clang/lld wrappers for targeting macOS.\n"
        "It does not contain an Apple macOS SDK. Supply your own SDK under SDK/\n"
        "and set `sdkRoot` in " + std::string(manifestFileName) + " to point at it.\n\n"
        "Wrappers in bin/ were generated by LXcode and are safe to regenerate.\n";
    auto saved = fs::writeTextAtomic(root / "README.md", readme, false);
    if (!saved) return Expected<std::filesystem::path>::failure(saved.error());
    return root;
}

std::vector<std::filesystem::path> candidateRoots() {
    std::vector<std::filesystem::path> roots;
    for (const char* name : {"LXCODE_SDK_ROOT", "MACOS_SDK_ROOT"}) {
        if (const char* value = std::getenv(name))
            if (*value) roots.emplace_back(value);
    }
    std::error_code ec;
    std::filesystem::path base;
    if (const char* env = std::getenv("LXCODE_SDK_BUNDLES")) base = env;
    else if (const char* home = std::getenv("HOME")) base = std::filesystem::path(home) / ".lxcode" / "sdk-bundles";
    if (!base.empty() && std::filesystem::is_directory(base, ec)) {
        for (std::filesystem::directory_iterator it(base, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            if (it->is_directory(ec) && isBundle(it->path())) roots.push_back(it->path());
        }
    }
    return roots;
}

Expected<Toolchain> detect(const std::filesystem::path& root) {
    auto manifestResult = readManifest(root);
    if (!manifestResult)
        return Expected<Toolchain>::failure(manifestResult.error());
    const auto manifest = *manifestResult;

    Toolchain toolchain;
    toolchain.kind = ToolchainKind::CustomSdk;
    toolchain.root = root.lexically_normal();
    toolchain.name = manifest.name.empty() ? "Custom SDK (" + root.filename().string() + ")"
                                           : manifest.name;
    toolchain.version = "bundle v" + std::to_string(manifest.version);

    const auto compiler = resolveCompiler(manifest.compiler, "clang");
    const auto compilerCXX = resolveCompiler(manifest.compilerCXX, "clang++");
    if (compiler.empty() || compilerCXX.empty())
        toolchain.errors.push_back("no clang/clang++ found for custom SDK bundle");
    else {
        toolchain.tools["clang"] = compiler;
        toolchain.tools["clang++"] = compilerCXX;
    }

    toolchain.architectures = manifest.architectures;
    if (toolchain.architectures.empty())
        toolchain.errors.push_back("SDK bundle manifest lists no architectures");
    std::sort(toolchain.architectures.begin(), toolchain.architectures.end());
    toolchain.architectures.erase(std::unique(toolchain.architectures.begin(),
                                             toolchain.architectures.end()),
                                  toolchain.architectures.end());
    for (const auto& architecture : toolchain.architectures) {
        if (!compiler.empty()) toolchain.tools["clang:" + architecture] = compiler;
        if (!compilerCXX.empty()) toolchain.tools["clang++:" + architecture] = compilerCXX;
    }

    const auto bin = root / "bin";
    if (!fs::exists(bin)) {
        auto written = writeWrappers(root, manifest);
        if (!written) toolchain.warnings.push_back("could not generate wrappers: " + written.error());
    }
    if (fs::exists(bin / "clang")) toolchain.tools["clang"] = bin / "clang";
    if (fs::exists(bin / "clang++")) toolchain.tools["clang++"] = bin / "clang++";
    for (const auto& architecture : toolchain.architectures) {
        if (fs::exists(bin / (architecture + "-apple-darwin-clang")))
            toolchain.tools["clang:" + architecture] = bin / (architecture + "-apple-darwin-clang");
        if (fs::exists(bin / (architecture + "-apple-darwin-clang++")))
            toolchain.tools["clang++:" + architecture] = bin / (architecture + "-apple-darwin-clang++");
    }
    for (const char* role : {"ar", "nm", "ranlib", "lipo"})
        if (fs::exists(bin / role)) toolchain.tools[role] = bin / role;

    if (!manifest.sdkRoot.empty()) {
        const auto configured = manifest.sdkRoot.is_absolute() ? manifest.sdkRoot
                                                               : root / manifest.sdkRoot;
        if (fs::exists(configured)) toolchain.sdk = configured.lexically_normal();
        else toolchain.errors.push_back("SDK path does not exist: " + configured.string());
    }
    if (toolchain.sdk.empty()) {
        const auto sdkRoot = root / "SDK";
        std::error_code ec;
        if (std::filesystem::is_directory(sdkRoot, ec)) {
            for (std::filesystem::directory_iterator it(sdkRoot, ec), end; it != end; it.increment(ec)) {
                if (ec) break;
                if (it->is_directory(ec) && strings::endsWith(it->path().filename().string(), ".sdk"))
                    toolchain.sdks.push_back(it->path());
            }
            std::sort(toolchain.sdks.begin(), toolchain.sdks.end(), versionNewer);
        }
        if (!toolchain.sdks.empty()) toolchain.sdk = toolchain.sdks.front();
        else toolchain.errors.push_back("no macOS SDK configured for bundle " + root.string());
    }
    if (!toolchain.sdk.empty()) toolchain.version += " / SDK " + toolchain.sdk.stem().string();

    if (manifest.linker.empty() || strings::toLower(manifest.linker) == "lld" ||
        strings::toLower(manifest.linker) == "ld64.lld") {
        if (which("ld.lld").empty() && which("lld").empty())
            toolchain.warnings.push_back("lld not found on PATH; linking may require a system linker");
    } else if (!fs::exists(manifest.linker) && which(manifest.linker).empty()) {
        toolchain.errors.push_back("linker not found: " + manifest.linker);
    }
    return toolchain;
}

std::filesystem::path toolFor(const Toolchain& toolchain, const std::string& architecture,
                              const std::string& role) {
    return lxcode::toolFor(toolchain, architecture, role);
}

std::vector<std::string> compileArguments(const Toolchain& toolchain,
                                          const std::filesystem::path& source,
                                          const std::filesystem::path& output,
                                          const osxcross::CompileOptions& options) {
    const auto compiler = customsdk::toolFor(toolchain, options.architecture, "clang++");
    if (compiler.empty()) return {};
    std::vector<std::string> args;
    args.push_back("--target=" + tripleFor(options.architecture));
    if (!toolchain.sdk.empty()) {
        args.push_back("-isysroot");
        args.push_back(toolchain.sdk.string());
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
                                       const osxcross::LinkOptions& options) {
    const auto linker = customsdk::toolFor(toolchain, options.architecture, "clang++");
    if (linker.empty()) return {};
    std::vector<std::string> args;
    args.push_back("--target=" + tripleFor(options.architecture));
    args.push_back("-fuse-ld=lld");
    if (!toolchain.sdk.empty()) {
        args.push_back("-isysroot");
        args.push_back(toolchain.sdk.string());
    }
    if (!options.deploymentTarget.empty())
        args.push_back("-mmacosx-version-min=" + options.deploymentTarget);
    for (const auto& dir : options.frameworkDirectories) args.push_back("-F" + dir);
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

} // namespace customsdk

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
        if (customsdk::isBundle(normalized)) {
            auto bundle = customsdk::detect(normalized);
            if (bundle) add(*bundle);
            continue;
        }
        auto toolchain = osxcross::detect(normalized);
        if (toolchain) add(*toolchain);
    }
    for (const auto& root : customsdk::candidateRoots()) {
        auto normalized = root.lexically_normal();
        if (seen.insert(normalized.string()).second && customsdk::isBundle(normalized)) {
            auto bundle = customsdk::detect(normalized);
            if (bundle) add(*bundle);
        }
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
            if ((toolchain.kind == ToolchainKind::OSXCross ||
                 toolchain.kind == ToolchainKind::CustomSdk) && toolchain.valid()) {
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
                const auto kind = item.value("kind", std::string("OSXCross"));
                if (kind == "CustomSdk" || kind == "custom-sdk") {
                    auto root = std::filesystem::path(item.value("root", ""));
                    if (customsdk::isBundle(root)) {
                        auto bundle = customsdk::detect(root);
                        if (bundle) {
                            if (item.contains("name") && !item["name"].get<std::string>().empty())
                                bundle->name = item["name"].get<std::string>();
                            add(*bundle);
                        }
                    }
                    continue;
                }
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
        if (toolchain.kind != ToolchainKind::OSXCross &&
            toolchain.kind != ToolchainKind::CustomSdk)
            continue;
        nlohmann::json item;
        item["kind"] = toolchainKindName(toolchain.kind);
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
    case ToolchainKind::CustomSdk: return "CustomSdk";
    case ToolchainKind::System: return "System";
    case ToolchainKind::CMake: return "CMake";
    }
    return "Unknown";
}

} // namespace lxcode
