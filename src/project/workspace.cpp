#include "lxcode/project/workspace.hpp"

#include "lxcode/core/filesystem.hpp"
#include "lxcode/core/strings.hpp"

#include <algorithm>
#include <set>

namespace lxcode {

Expected<XcWorkspace> XcWorkspace::open(const std::filesystem::path& path) {
    XcWorkspace result;
    result.path_ = path;
    std::filesystem::path dataPath = path;
    if (path.filename() != "contents.xcworkspacedata")
        dataPath /= "contents.xcworkspacedata";
    auto text = fs::readText(dataPath);
    if (!text) return Expected<XcWorkspace>::failure(text.error());
    auto parsed = result.document_.parse(*text);
    if (!parsed) return Expected<XcWorkspace>::failure(parsed.error());
    return result;
}

Expected<XcWorkspace> XcWorkspace::create(const std::filesystem::path& directory,
                                          const std::string& name) {
    XcWorkspace result;
    result.path_ = directory / (name + ".xcworkspace");
    xml::Node workspace;
    workspace.name = "Workspace";
    workspace.attributes.push_back({"version", "1.0"});
    result.document_.root() = workspace;
    auto saved = result.save();
    if (!saved) return Expected<XcWorkspace>::failure(saved.error());
    return result;
}

void collectFileRefs(const xml::Node& node, std::vector<WorkspaceFileRef>& out) {
    if (node.name == "FileRef")
        out.push_back({node.attributeValue("location"), node.attributeValue("name")});
    for (const auto& child : node.children) collectFileRefs(child, out);
}

std::vector<WorkspaceFileRef> XcWorkspace::fileReferences() const {
    std::vector<WorkspaceFileRef> result;
    collectFileRefs(document_.root(), result);
    return result;
}

std::vector<std::filesystem::path> XcWorkspace::projectPaths() const {
    std::vector<std::filesystem::path> result;
    std::set<std::string> seen;
    for (const auto& ref : fileReferences()) {
        auto location = ref.location;
        const auto colon = location.find(':');
        if (colon != std::string::npos) location = location.substr(colon + 1);
        if (location.empty()) continue;
        if (!strings::endsWith(location, ".xcodeproj")) continue;
        auto absolute = std::filesystem::path(location);
        if (absolute.is_relative()) absolute = directory() / absolute;
        absolute = absolute.lexically_normal();
        if (seen.insert(absolute.string()).second) result.push_back(absolute);
    }
    return result;
}

std::vector<std::string> XcWorkspace::schemes() const {
    std::set<std::string> names;
    auto dataPath = path_;
    if (path_.filename() != "contents.xcworkspacedata") dataPath /= "contents.xcworkspacedata";
    for (const auto& file : fs::filesWithExtension(dataPath.parent_path(), "xcscheme")) {
        names.insert(file.stem().string());
    }
    for (const auto& project : projectPaths()) {
        auto shared = project / "xcshareddata" / "xcschemes";
        for (const auto& file : fs::filesWithExtension(shared, "xcscheme"))
            names.insert(file.stem().string());
    }
    return {names.begin(), names.end()};
}

Expected<void> XcWorkspace::save() const {
    auto dataPath = path_;
    if (path_.filename() != "contents.xcworkspacedata") dataPath /= "contents.xcworkspacedata";
    return fs::writeTextAtomic(dataPath, document_.serialize());
}

} // namespace lxcode
