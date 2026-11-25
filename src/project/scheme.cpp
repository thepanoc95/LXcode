#include "lxcode/project/scheme.hpp"

#include "lxcode/core/filesystem.hpp"

#include <algorithm>

namespace lxcode {

namespace {

void collectNamed(const xml::Node& node, const std::string& name, std::vector<const xml::Node*>& out) {
    if (node.name == name) out.push_back(&node);
    for (const auto& child : node.children) collectNamed(child, name, out);
}

xml::Node makeBuildableNode(const BuildableReference& reference) {
    xml::Node node;
    node.name = "BuildableReference";
    node.attributes.push_back({"BuildableIdentifier", "primary"});
    node.attributes.push_back({"BlueprintIdentifier", reference.blueprintIdentifier});
    node.attributes.push_back({"BuildableName", reference.buildableName});
    node.attributes.push_back({"BlueprintName", reference.blueprintName});
    node.attributes.push_back({"ReferencedContainer", reference.referencedContainer});
    return node;
}

xml::Node makePostActions() {
    xml::Node actions;
    actions.name = "PostActions";
    return actions;
}

} // namespace

BuildableReference buildableFromNode(const xml::Node& node) {
    BuildableReference result;
    result.name = node.attributeValue("BlueprintName", node.attributeValue("BuildableName"));
    result.blueprintIdentifier = node.attributeValue("BlueprintIdentifier");
    result.buildableName = node.attributeValue("BuildableName");
    result.blueprintName = node.attributeValue("BlueprintName");
    result.referencedContainer = node.attributeValue("ReferencedContainer");
    result.targetIdentifier = node.attributeValue("TargetIdentifier");
    return result;
}

Expected<SchemeDocument> SchemeDocument::open(const std::filesystem::path& path) {
    SchemeDocument result;
    result.path_ = path;
    auto text = fs::readText(path);
    if (!text) return Expected<SchemeDocument>::failure(text.error());
    auto parsed = result.document_.parse(*text);
    if (!parsed) return Expected<SchemeDocument>::failure(parsed.error());
    if (result.document_.root().name != "Scheme")
        return Expected<SchemeDocument>::failure("not an .xcscheme document: " + path.string());
    return result;
}

SchemeDocument SchemeDocument::create(const std::string& name, const BuildableReference& reference,
                                      const std::string& configuration) {
    SchemeDocument result;
    result.document_.root().name = "Scheme";
    result.document_.root().setAttribute("LastUpgradeVersion", "1600");
    result.document_.root().setAttribute("version", "1.7");

    xml::Node buildAction;
    buildAction.name = "BuildAction";
    buildAction.setAttribute("parallelizeBuildables", "YES");
    buildAction.setAttribute("buildImplicitDependencies", "YES");
    xml::Node entries;
    entries.name = "BuildActionEntries";
    xml::Node entry;
    entry.name = "BuildActionEntry";
    entry.setAttribute("buildForTesting", "YES");
    entry.setAttribute("buildForRunning", "YES");
    entry.setAttribute("buildForProfiling", "YES");
    entry.setAttribute("buildForArchiving", "YES");
    entry.setAttribute("buildForAnalyzing", "YES");
    entry.children.push_back(makeBuildableNode(reference));
    entries.children.push_back(entry);
    buildAction.children.push_back(entries);
    result.document_.root().children.push_back(buildAction);

    xml::Node testAction;
    testAction.name = "TestAction";
    testAction.setAttribute("buildConfiguration", configuration);
    testAction.setAttribute("selectedDebuggerIdentifier", "Xcode.DebuggerFoundation.Debugger.LLDB");
    testAction.setAttribute("selectedLauncherIdentifier", "Xcode.IDEFoundation.Launcher.PosixSpawn");
    testAction.setAttribute("shouldUseLaunchSchemeArgsEnv", "YES");
    testAction.children.push_back(makePostActions());
    result.document_.root().children.push_back(testAction);

    xml::Node launchAction;
    launchAction.name = "LaunchAction";
    launchAction.setAttribute("buildConfiguration", configuration);
    launchAction.setAttribute("selectedDebuggerIdentifier", "Xcode.DebuggerFoundation.Debugger.LLDB");
    launchAction.setAttribute("selectedLauncherIdentifier", "Xcode.IDEFoundation.Launcher.PosixSpawn");
    launchAction.setAttribute("launchStyle", "0");
    launchAction.setAttribute("useCustomWorkingDirectory", "NO");
    launchAction.setAttribute("ignoresPersistentStateOnLaunch", "NO");
    launchAction.setAttribute("debugDocumentVersioning", "YES");
    launchAction.setAttribute("debugServiceExtension", "internal");
    launchAction.setAttribute("allowLocationSimulation", "YES");
    xml::Node runnable;
    runnable.name = "BuildableProductRunnable";
    runnable.setAttribute("runnableDebuggingMode", "0");
    runnable.children.push_back(makeBuildableNode(reference));
    launchAction.children.push_back(runnable);
    launchAction.children.push_back(makePostActions());
    result.document_.root().children.push_back(launchAction);

    for (const char* actionName : {"ProfileAction", "AnalyzeAction", "ArchiveAction"}) {
        xml::Node action;
        action.name = actionName;
        action.setAttribute("buildConfiguration", configuration);
        if (std::string(actionName) == "ProfileAction") {
            xml::Node runnable;
            runnable.name = "BuildableProductRunnable";
            runnable.setAttribute("runnableDebuggingMode", "0");
            runnable.children.push_back(makeBuildableNode(reference));
            action.children.push_back(runnable);
        }
        action.children.push_back(makePostActions());
        result.document_.root().children.push_back(action);
    }
    result.path_ = name + ".xcscheme";
    return result;
}

std::string SchemeDocument::name() const {
    if (!path_.empty()) return path_.stem().string();
    return document_.root().attributeValue("name", "Scheme");
}

std::vector<BuildableReference> SchemeDocument::buildableReferences() const {
    std::vector<const xml::Node*> nodes;
    collectNamed(document_.root(), "BuildableReference", nodes);
    std::vector<BuildableReference> result;
    for (const auto* node : nodes) result.push_back(buildableFromNode(*node));
    return result;
}

std::vector<BuildableReference> SchemeDocument::buildEntries(const std::string& actionName) const {
    std::vector<BuildableReference> result;
    const auto* action = document_.root().child(actionName);
    if (!action) return result;
    std::vector<const xml::Node*> nodes;
    collectNamed(*action, "BuildableReference", nodes);
    for (const auto* node : nodes) result.push_back(buildableFromNode(*node));
    return result;
}

std::string SchemeDocument::buildConfiguration(const std::string& actionName) const {
    const auto* action = document_.root().child(actionName);
    if (action) {
        const auto value = action->attributeValue("buildConfiguration");
        if (!value.empty()) return value;
    }
    // Xcode stores the default configuration on LaunchAction; BuildAction has none.
    if (actionName != "LaunchAction") return buildConfiguration("LaunchAction");
    return {};
}

void SchemeDocument::setBuildConfiguration(const std::string& actionName, std::string configuration) {
    auto* action = document_.root().child(actionName);
    if (action) action->setAttribute("buildConfiguration", std::move(configuration));
}

Expected<void> SchemeDocument::save() const {
    return fs::writeTextAtomic(path_, document_.serialize());
}

} // namespace lxcode
