#include "test_support.hpp"

#include "lxcode/project/xcodeproject.hpp"
#include "lxcode/core/filesystem.hpp"

#include <algorithm>
#include <functional>

using namespace lxcode;
using namespace lxcode::test;

int main() {
    const auto root = tempDirectory("project");
    const auto projectPath = root / "Demo.xcodeproj";
    auto created = XcodeProject::create(projectPath, "Demo");
    LXCHECK_TRUE(created);
    LXCHECK_EQ(created->name(), std::string("Demo"));
    LXCHECK_TRUE(fs::exists(projectPath / "project.pbxproj"));
    LXCHECK_TRUE(fs::exists(root / "main.cpp"));

    auto targets = created->targets();
    LXCHECK_EQ(targets.size(), std::size_t(1));
    LXCHECK_EQ(targets.front().name, std::string("Demo"));
    LXCHECK_TRUE(!targets.front().phases.empty());

    writeFile(root / "extra.cpp", "int extra() { return 7; }\n");
    auto added = created->addFile(root / "extra.cpp", {}, targets.front().id);
    LXCHECK_TRUE(added);
    LXCHECK_TRUE(created->document().validate().empty());

    auto settings = created->buildSettings(targets.front().id, "Debug");
    LXCHECK_TRUE(settings.count("SDKROOT") == 1);
    created->setBuildSetting(targets.front().id, "Debug", "CUSTOM_FLAG", "YES");
    settings = created->buildSettings(targets.front().id, "Debug");
    LXCHECK_EQ(settings["CUSTOM_FLAG"], std::string("YES"));

    LXCHECK_TRUE(created->addGroup("Helpers"));
    LXCHECK_TRUE(created->save());

    auto reopened = XcodeProject::open(projectPath);
    LXCHECK_TRUE(reopened);
    LXCHECK_EQ(reopened->name(), std::string("Demo"));
    LXCHECK_TRUE(reopened->document().fieldString(*added, "lastKnownFileType").find("sourcecode") == 0);
    auto reopenedTargets = reopened->targets();
    LXCHECK_EQ(reopenedTargets.size(), std::size_t(1));

    const auto navigator = reopened->navigator();
    LXCHECK_TRUE(!navigator.empty());
    bool foundExtra = false;
    std::function<void(const std::vector<NavigatorItem>&)> walk =
        [&](const std::vector<NavigatorItem>& items) {
            for (const auto& item : items) {
                if (item.name == "extra.cpp") foundExtra = true;
                walk(item.children);
            }
        };
    walk(navigator);
    LXCHECK_TRUE(foundExtra);

    LXCHECK_TRUE(reopened->renameFile(*added, "renamed.cpp", true));
    LXCHECK_TRUE(fs::exists(root / "renamed.cpp"));
    LXCHECK_TRUE(reopened->removeFile(*added));
    LXCHECK_TRUE(reopened->document().object(*added) == nullptr);
    LXCHECK_TRUE(reopened->save());

    auto final = XcodeProject::open(projectPath);
    LXCHECK_TRUE(final);
    LXCHECK_TRUE(final->document().object(*added) == nullptr);

    std::cout << "project tests passed\n";
    return 0;
}
