#include "test_support.hpp"

#include "lxcode/toolchains/toolchain.hpp"

#include <algorithm>
#include <filesystem>

using namespace lxcode;
using namespace lxcode::test;

int main() {
    const auto root = tempDirectory("toolchain");
    const auto bin = root / "bin";
    std::filesystem::create_directories(bin);
    std::filesystem::create_directories(root / "SDK" / "MacOSX14.4.sdk");

    for (const auto& name : {"arm64-apple-darwin-clang", "arm64-apple-darwin-clang++",
                             "x86_64-apple-darwin-clang", "x86_64-apple-darwin-clang++",
                             "ld64.lld", "lipo", "ar", "nm", "ranlib"}) {
        writeFile(bin / name, "#!/bin/sh\nexit 0\n");
        std::filesystem::permissions(bin / name, std::filesystem::perms::owner_exec,
                                     std::filesystem::perm_options::add);
    }
    writeFile(bin / "osxcross-conf", "#!/bin/sh\necho 'osxcross 0.3'\n");
    std::filesystem::permissions(bin / "osxcross-conf", std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::add);

    auto detected = osxcross::detect(root);
    LXCHECK_TRUE(detected);
    LXCHECK_TRUE(detected->valid());
    LXCHECK_EQ(detected->kind, ToolchainKind::OSXCross);
    LXCHECK_TRUE(detected->sdk.string().find("MacOSX14.4.sdk") != std::string::npos);
    LXCHECK_TRUE(detected->supports("arm64"));
    LXCHECK_TRUE(detected->supports("x86_64"));
    LXCHECK_EQ(detected->version, std::string("osxcross 0.3"));

    const auto compiler = osxcross::toolFor(*detected, "arm64", "clang++");
    LXCHECK_TRUE(compiler.string().find("arm64-apple-darwin-clang++") != std::string::npos);
    LXCHECK_TRUE(osxcross::toolFor(*detected, "x86_64", "lipo").empty() == false);

    osxcross::CompileOptions compile;
    compile.architecture = "arm64";
    compile.deploymentTarget = "12.0";
    compile.standard = "c++20";
    compile.includeDirectories = {"/usr/local/include"};
    compile.defines = {"DEBUG=1"};
    compile.flags = {"-Wall"};
    auto compileArgs = osxcross::compileArguments(*detected, "main.cpp", "main.o", compile);
    auto contains = [&](const std::string& value) {
        return std::find(compileArgs.begin(), compileArgs.end(), value) != compileArgs.end();
    };
    LXCHECK_TRUE(contains("-isysroot"));
    LXCHECK_TRUE(contains("-arch"));
    LXCHECK_TRUE(contains("arm64"));
    LXCHECK_TRUE(contains("-mmacosx-version-min=12.0"));
    LXCHECK_TRUE(contains("-std=c++20"));
    LXCHECK_TRUE(contains("-I/usr/local/include"));
    LXCHECK_TRUE(contains("-DDEBUG=1"));
    LXCHECK_TRUE(contains("-c"));

    osxcross::LinkOptions link;
    link.architecture = "arm64";
    link.frameworks = {"Cocoa"};
    link.libraries = {"c++"};
    auto linkArgs = osxcross::linkArguments(*detected, {"main.o"}, "Demo", link);
    auto has = [&](const std::string& value) {
        return std::find(linkArgs.begin(), linkArgs.end(), value) != linkArgs.end();
    };
    LXCHECK_TRUE(has("-framework"));
    LXCHECK_TRUE(has("Cocoa"));
    LXCHECK_TRUE(has("-lc++"));
    LXCHECK_TRUE(has("-o"));

    ToolchainRegistry registry;
    registry.addPath(root);
    registry.detect();
    LXCHECK_TRUE(registry.find(detected->name) != nullptr);
    const auto configPath = root / "toolchains.json";
    LXCHECK_TRUE(registry.saveConfiguration(configPath));
    ToolchainRegistry loaded;
    LXCHECK_TRUE(loaded.loadConfiguration(configPath));
    LXCHECK_TRUE(!loaded.toolchains().empty());

    auto bad = osxcross::detect(root / "missing");
    LXCHECK_TRUE(bad.ok());
    LXCHECK_TRUE(!bad->valid());
    LXCHECK_TRUE(!bad->errors.empty());

    std::cout << "toolchain tests passed\n";
    return 0;
}
