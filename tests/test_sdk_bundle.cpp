#include "test_support.hpp"

#include "lxcode/build/buildsystem.hpp"
#include "lxcode/core/filesystem.hpp"
#include "lxcode/core/process.hpp"
#include "lxcode/toolchains/toolchain.hpp"

#include <algorithm>
#include <filesystem>

using namespace lxcode;
using namespace lxcode::test;

namespace {

bool contains(const std::vector<std::string>& values, const std::string& expected) {
    return std::find(values.begin(), values.end(), expected) != values.end();
}

} // namespace

int main() {
    const auto root = tempDirectory("sdk-bundle") / "MyMacSdk";

    customsdk::Manifest manifest;
    manifest.name = "Test macOS SDK";
    manifest.compiler = which("clang");
    manifest.compilerCXX = which("clang++");
    manifest.architectures = {"arm64", "x86_64"};
    manifest.deploymentTarget = "12.0";

    LXCHECK_TRUE(customsdk::isBundle(root) == false);
    auto created = customsdk::createBundle(root, manifest);
    LXCHECK_TRUE(created);
    LXCHECK_TRUE(customsdk::isBundle(root));
    LXCHECK_TRUE(fs::exists(root / customsdk::manifestFileName));
    LXCHECK_TRUE(fs::exists(root / "README.md"));
    LXCHECK_TRUE(fs::exists(root / "SDK"));
    LXCHECK_TRUE(fs::exists(root / "bin" / "clang"));
    LXCHECK_TRUE(fs::exists(root / "bin" / "clang++"));
    LXCHECK_TRUE(fs::exists(root / "bin" / "arm64-apple-darwin-clang"));
    LXCHECK_TRUE(fs::exists(root / "bin" / "x86_64-apple-darwin-clang++"));
    LXCHECK_TRUE(fs::exists(root / "bin" / "lipo"));

    const auto wrapper = readFile(root / "bin" / "arm64-apple-darwin-clang++");
    LXCHECK_TRUE(wrapper.find("--target=arm64-apple-darwin") != std::string::npos);

    auto reread = customsdk::readManifest(root);
    LXCHECK_TRUE(reread);
    LXCHECK_EQ(reread->name, std::string("Test macOS SDK"));
    LXCHECK_TRUE(reread->compiler == manifest.compiler);
    LXCHECK_EQ(reread->deploymentTarget, std::string("12.0"));
    LXCHECK_EQ(reread->architectures.size(), std::size_t(2));

    // Without an SDK the bundle is detectable but invalid.
    auto incomplete = customsdk::detect(root);
    LXCHECK_TRUE(incomplete);
    LXCHECK_EQ(incomplete->kind, ToolchainKind::CustomSdk);
    LXCHECK_TRUE(!incomplete->valid());
    LXCHECK_TRUE(!incomplete->errors.empty());

    const auto sdk = root / "SDK" / "MacOSX14.4.sdk";
    std::filesystem::create_directories(sdk);
    auto detected = customsdk::detect(root);
    LXCHECK_TRUE(detected);
    LXCHECK_TRUE(detected->valid());
    LXCHECK_EQ(detected->kind, ToolchainKind::CustomSdk);
    LXCHECK_EQ(detected->name, std::string("Test macOS SDK"));
    LXCHECK_TRUE(detected->sdk == sdk);
    LXCHECK_TRUE(detected->supports("arm64"));
    LXCHECK_TRUE(detected->supports("x86_64"));
    LXCHECK_TRUE(toolchainKindName(detected->kind) == "CustomSdk");

    const auto compiler = customsdk::toolFor(*detected, "arm64", "clang++");
    LXCHECK_TRUE(compiler.string().find("arm64-apple-darwin-clang++") != std::string::npos);
    LXCHECK_TRUE(!customsdk::toolFor(*detected, "arm64", "lipo").empty());

    osxcross::CompileOptions compile;
    compile.architecture = "arm64";
    compile.deploymentTarget = "12.0";
    compile.standard = "c++20";
    compile.includeDirectories = {"/opt/include"};
    compile.defines = {"DEBUG=1"};
    compile.flags = {"-Wall"};
    auto compileArgs = customsdk::compileArguments(*detected, "main.cpp", "main.o", compile);
    LXCHECK_TRUE(contains(compileArgs, "--target=arm64-apple-darwin"));
    LXCHECK_TRUE(contains(compileArgs, "-isysroot"));
    LXCHECK_TRUE(contains(compileArgs, sdk.string()));
    LXCHECK_TRUE(contains(compileArgs, "-mmacosx-version-min=12.0"));
    LXCHECK_TRUE(contains(compileArgs, "-std=c++20"));
    LXCHECK_TRUE(contains(compileArgs, "-I/opt/include"));
    LXCHECK_TRUE(contains(compileArgs, "-DDEBUG=1"));
    LXCHECK_TRUE(contains(compileArgs, "-c"));

    osxcross::LinkOptions link;
    link.architecture = "x86_64";
    link.frameworks = {"Cocoa"};
    link.libraries = {"c++"};
    auto linkArgs = customsdk::linkArguments(*detected, {"main.o"}, "Demo", link);
    LXCHECK_TRUE(contains(linkArgs, "--target=x86_64-apple-darwin"));
    LXCHECK_TRUE(contains(linkArgs, "-fuse-ld=lld"));
    LXCHECK_TRUE(contains(linkArgs, "-isysroot"));
    LXCHECK_TRUE(contains(linkArgs, "-framework"));
    LXCHECK_TRUE(contains(linkArgs, "Cocoa"));
    LXCHECK_TRUE(contains(linkArgs, "-lc++"));
    LXCHECK_TRUE(contains(linkArgs, "-o"));

    ToolchainRegistry registry;
    registry.addPath(root);
    registry.detect();
    LXCHECK_TRUE(registry.find(detected->name) != nullptr);
    LXCHECK_TRUE(registry.selected());
    LXCHECK_EQ(registry.selected()->kind, ToolchainKind::CustomSdk);

    const auto configPath = root / "toolchains.json";
    LXCHECK_TRUE(registry.saveConfiguration(configPath));
    ToolchainRegistry loaded;
    LXCHECK_TRUE(loaded.loadConfiguration(configPath));
    LXCHECK_TRUE(loaded.find(detected->name) != nullptr);

    const auto toolchainFile = root / "toolchain.cmake";
    LXCHECK_TRUE(generateToolchainFile(*detected, "arm64", toolchainFile));
    const auto text = readFile(toolchainFile);
    LXCHECK_TRUE(text.find("CMAKE_SYSTEM_NAME Darwin") != std::string::npos);
    LXCHECK_TRUE(text.find("CMAKE_OSX_ARCHITECTURES arm64") != std::string::npos);
    LXCHECK_TRUE(text.find("-fuse-ld=lld") != std::string::npos);
    LXCHECK_TRUE(text.find("arm64-apple-darwin-clang++") != std::string::npos);

    const auto invalidRoot = tempDirectory("sdk-bundle-invalid");
    writeFile(invalidRoot / customsdk::manifestFileName, "{\"format\":\"something-else\"}");
    auto invalid = customsdk::readManifest(invalidRoot);
    LXCHECK_TRUE(!invalid);

    auto missing = customsdk::detect(root / "does-not-exist");
    LXCHECK_TRUE(!missing);

    std::cout << "sdk bundle tests passed\n";
    return 0;
}
