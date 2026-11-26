#include "test_support.hpp"

#include "lxcode/core/filesystem.hpp"
#include "lxcode/core/strings.hpp"
#include "lxcode/project/plist.hpp"
#include "lxcode/xcodesdk/linuxbundle.hpp"

#include <algorithm>
#include <filesystem>

using namespace lxcode;
using namespace lxcode::test;
using namespace lxcode::xcodesdk;

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool hasError(const Validation& validation, const std::string& needle) {
    return std::any_of(validation.errors.begin(), validation.errors.end(),
                       [&](const std::string& value) { return contains(value, needle); });
}

} // namespace

int main() {
    LXCHECK_EQ(llvmTarget("x86_64"), std::string("x86_64-unknown-linux-gnu"));
    LXCHECK_EQ(llvmTarget("arm64"), std::string("aarch64-unknown-linux-gnu"));
    LXCHECK_EQ(llvmTarget("aarch64"), std::string("aarch64-unknown-linux-gnu"));
    LXCHECK_TRUE(llvmTarget("sparc").empty());

    const auto root = tempDirectory("xcodesdk") / "LinuxBundle";
    Manifest manifest;
    manifest.name = "LXcode Linux";
    manifest.versionString = "2.4";
    manifest.architectures = {"x86_64", "arm64"};

    auto created = createBundle(root, manifest);
    LXCHECK_TRUE(created);
    LXCHECK_TRUE(fs::exists(root / manifestFileName));
    LXCHECK_TRUE(fs::exists(root / "README.md"));
    LXCHECK_TRUE(fs::exists(root / "install.sh"));
    LXCHECK_TRUE(fs::exists(root / "Linux.xcconfig"));
    LXCHECK_TRUE(fs::exists(root / platformName / "Info.plist"));
    LXCHECK_TRUE(fs::exists(root / toolchainName / "Info.plist"));
    LXCHECK_TRUE(fs::exists(root / toolchainName / "usr" / "bin" / "clang"));
    LXCHECK_TRUE(fs::exists(root / toolchainName / "usr" / "bin" / "clang++"));
    LXCHECK_TRUE(fs::exists(root / toolchainName / "usr" / "bin" / "ar"));
    LXCHECK_TRUE(fs::exists(root / toolchainName / "usr" / "bin" / "ld"));
    LXCHECK_TRUE(fs::exists(root / "sysroot" / "README.md"));

    auto reread = readManifest(root);
    LXCHECK_TRUE(reread);
    LXCHECK_EQ(reread->name, std::string("LXcode Linux"));
    LXCHECK_EQ(reread->versionString, std::string("2.4"));
    LXCHECK_EQ(reread->architectures.size(), std::size_t(2));
    LXCHECK_TRUE(reread->sysroot.empty());

    // Generated plists must be real, parseable XML plists with Xcode's keys.
    plist::Document settings;
    auto settingsText = readFile(root / platformName / "Developer" / "SDKs" / sdkName /
                                 "SDKSettings.plist");
    LXCHECK_TRUE(settings.parse(settingsText));
    LXCHECK_EQ(settings.valueAt("CanonicalName"), std::string("linux"));
    LXCHECK_EQ(settings.valueAt("isBaseSDK"), std::string("YES"));
    LXCHECK_EQ(settings.valueAt("Version"), std::string("2.4"));
    LXCHECK_EQ(settings.valueAt("DefaultProperties.PLATFORM_NAME"), std::string("linux"));
    LXCHECK_EQ(settings.valueAt("CustomProperties.LINUX_SDK_BUNDLE"),
               std::string("$(SDK_DIR)/../../../.."));
    const auto* toolchainsValue = settings.root().find("Toolchains");
    LXCHECK_TRUE(toolchainsValue && !toolchainsValue->array.empty());
    LXCHECK_EQ(toolchainsValue->array.front().text, std::string("dev.lxcode.sdk.linux"));
    const auto* archsValue = settings.root().find("SupportedArchs");
    LXCHECK_TRUE(archsValue && archsValue->array.size() == 2);

    plist::Document toolchainInfo;
    LXCHECK_TRUE(toolchainInfo.parse(readFile(root / toolchainName / "Info.plist")));
    LXCHECK_EQ(toolchainInfo.valueAt("Identifier"), std::string("dev.lxcode.sdk.linux"));
    LXCHECK_EQ(toolchainInfo.valueAt("CompatibilityVersion"), std::string("2"));

    const auto bin = root / toolchainName / "usr" / "bin";
    const auto armClang = readFile(bin / "arm64-unknown-linux-gnu-clang");
    LXCHECK_TRUE(contains(armClang, "--target='aarch64-unknown-linux-gnu'"));
    LXCHECK_TRUE(contains(armClang, "${BUNDLE_DIR}/sysroot"));
    LXCHECK_TRUE(contains(armClang, "CDPATH="));
    LXCHECK_TRUE(contains(armClang, "-arch) shift;")); // Xcode -arch must be stripped
    const auto x86Clangxx = readFile(bin / "x86_64-unknown-linux-gnu-clang++");
    LXCHECK_TRUE(contains(x86Clangxx, "--target='x86_64-unknown-linux-gnu'"));
    LXCHECK_TRUE(contains(x86Clangxx, "clang++"));

    const auto xcconfig = readFile(root / "Linux.xcconfig");
    for (const auto* key : {"LINUX_ARCH", "ARCHS", "SDKROOT", "CC", "CXX", "AR", "NM",
                            "RANLIB", "OTHER_LDFLAGS", "CODE_SIGNING_ALLOWED"})
        LXCHECK_TRUE(contains(xcconfig, std::string(key) + " = "));
    LXCHECK_TRUE(contains(xcconfig, "Linux.platform/Developer/SDKs/Linux.sdk"));
    LXCHECK_TRUE(contains(xcconfig, "$(LINUX_ARCH)-unknown-linux-gnu-clang"));

    auto valid = validate(root);
    LXCHECK_TRUE(valid.ok());
    LXCHECK_TRUE(valid.errors.empty());
    LXCHECK_TRUE(!valid.warnings.empty()); // empty sysroot and PATH-resolved tools

    writeFile(root / "sysroot" / "usr" / "include" / "stdio.h", "/* test header */\n");
    writeFile(root / "sysroot" / "usr" / "lib" / "libc.so", "/* test library */\n");
    auto populated = validate(root);
    LXCHECK_TRUE(populated.ok());
    LXCHECK_TRUE(!contains(strings::join(populated.warnings, "\n"), "sysroot is incomplete"));

    // Custom output path for the xcconfig.
    const auto customConfig = root / "Configs" / "Project.xcconfig";
    auto generatedConfig = generateXcconfig(root, customConfig);
    LXCHECK_TRUE(generatedConfig);
    LXCHECK_EQ(generatedConfig->string(), customConfig.string());
    LXCHECK_TRUE(contains(readFile(customConfig), "SDKROOT = "));

    // Dry-run install reports targets without touching the developer directory.
    const auto developerDir = tempDirectory("xcodesdk-developer");
    auto dryRun = install(root, developerDir, true);
    LXCHECK_TRUE(dryRun);
    LXCHECK_EQ(dryRun->size(), std::size_t(2));
    LXCHECK_TRUE(!fs::exists(developerDir / platformName));
    LXCHECK_TRUE(!fs::exists(developerDir / toolchainName));

    auto installed = install(root, developerDir, false);
    LXCHECK_TRUE(installed);
    LXCHECK_TRUE(fs::exists(developerDir / "Platforms" / platformName / "Info.plist"));
    LXCHECK_TRUE(fs::exists(developerDir / "Toolchains" / toolchainName / "usr" / "bin" / "clang"));

    // Reinstalling replaces stale generated content.
    const auto stale = developerDir / "Toolchains" / toolchainName / "usr" / "bin" / "stale";
    writeFile(stale, "stale\n");
    LXCHECK_TRUE(fs::exists(stale));
    auto reinstalled = install(root, developerDir, false);
    LXCHECK_TRUE(reinstalled);
    LXCHECK_TRUE(!fs::exists(stale));

    // Manifest updates regenerate wrappers without destroying the sysroot.
    manifest.architectures = {"x86_64"};
    manifest.sysroot = "/opt/linux-sysroot";
    manifest.clang = "/usr/bin/clang";
    auto updated = createBundle(root, manifest);
    LXCHECK_TRUE(updated);
    LXCHECK_TRUE(!fs::exists(root / toolchainName / "usr" / "bin" /
                             "arm64-unknown-linux-gnu-clang"));
    LXCHECK_TRUE(contains(readFile(bin / "clang"), "/opt/linux-sysroot"));
    LXCHECK_TRUE(fs::exists(root / "sysroot" / "usr" / "include" / "stdio.h"));

    auto explicitSysroot = validate(root);
    LXCHECK_TRUE(!explicitSysroot.ok()); // /opt/linux-sysroot does not exist here
    LXCHECK_TRUE(hasError(explicitSysroot, "sysroot not found"));

    // Invalid bundles are reported rather than silently accepted.
    const auto invalid = tempDirectory("xcodesdk-invalid");
    auto invalidValidation = validate(invalid);
    LXCHECK_TRUE(!invalidValidation.ok());
    LXCHECK_TRUE(hasError(invalidValidation, "manifest"));
    auto rejected = createBundle(invalid / "bad", [] {
        Manifest bad;
        bad.architectures = {"sparc"};
        return bad;
    }());
    LXCHECK_TRUE(!rejected);

    std::cout << "xcode sdk bundle tests passed\n";
    return 0;
}
