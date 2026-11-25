#include "test_support.hpp"

#include "lxcode/project/plist.hpp"
#include "lxcode/project/scheme.hpp"
#include "lxcode/project/workspace.hpp"
#include "lxcode/project/xcconfig.hpp"

#include <algorithm>

using namespace lxcode;
using namespace lxcode::test;

int main() {
    const auto root = tempDirectory("plists");

    const std::string xmlPlist = R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleIdentifier</key>
  <string>org.lxcode.demo</string>
  <key>CFBundleVersion</key>
  <string>3</string>
  <key>LSMinimumSystemVersion</key>
  <string>11.0</string>
</dict>
</plist>
)";
    plist::Document document;
    auto parsed = document.parse(xmlPlist);
    LXCHECK_TRUE(parsed);
    LXCHECK_EQ(document.valueAt("CFBundleIdentifier"), std::string("org.lxcode.demo"));
    LXCHECK_EQ(document.valueAt("CFBundleVersion"), std::string("3"));
    LXCHECK_TRUE(document.setValueAt("CFBundleVersion", "4"));
    LXCHECK_EQ(document.valueAt("CFBundleVersion"), std::string("4"));
    const auto serializedXml = document.serialize();
    LXCHECK_TRUE(serializedXml.find("org.lxcode.demo") != std::string::npos);

    plist::Document openStep;
    auto openStepParsed = openStep.parse("{ Name = Demo; Version = 2; Enabled = YES; }");
    LXCHECK_TRUE(openStepParsed);
    LXCHECK_EQ(openStep.valueAt("Name"), std::string("Demo"));
    LXCHECK_EQ(openStep.valueAt("Version"), std::string("2"));

    XcconfigDocument config;
    auto configParsed = config.parse(R"(# comment
#include "Shared.xcconfig"
PRODUCT_NAME = Demo
GCC_PREPROCESSOR_DEFINITIONS = DEBUG=1 $(inherited)
)");
    LXCHECK_TRUE(configParsed);
    LXCHECK_TRUE(!config.includes().empty());
    LXCHECK_EQ(config.setting("PRODUCT_NAME"), std::string("Demo"));
    config.setSetting("SDKROOT", "macosx");
    LXCHECK_EQ(config.setting("SDKROOT"), std::string("macosx"));
    LXCHECK_TRUE(config.removeSetting("SDKROOT"));
    LXCHECK_TRUE(config.setting("SDKROOT").empty());

    auto workspace = XcWorkspace::create(root, "Demo");
    LXCHECK_TRUE(workspace);
    LXCHECK_TRUE(workspace->save());
    auto reopenedWorkspace = XcWorkspace::open(root / "Demo.xcworkspace");
    LXCHECK_TRUE(reopenedWorkspace);

    BuildableReference reference;
    reference.name = "Demo";
    reference.buildableName = "Demo";
    reference.blueprintName = "Demo";
    reference.blueprintIdentifier = "A1B2C3";
    reference.referencedContainer = "container:Demo.xcodeproj";
    auto scheme = SchemeDocument::create("Demo", reference, "Debug");
    scheme.setPath(root / "Demo.xcscheme");
    LXCHECK_TRUE(scheme.save());
    auto reopenedScheme = SchemeDocument::open(root / "Demo.xcscheme");
    LXCHECK_TRUE(reopenedScheme);
    LXCHECK_EQ(reopenedScheme->name(), std::string("Demo"));
    LXCHECK_EQ(reopenedScheme->buildConfiguration("BuildAction"), std::string("Debug"));
    LXCHECK_TRUE(!reopenedScheme->buildableReferences().empty());

    std::cout << "plist/config tests passed\n";
    return 0;
}
