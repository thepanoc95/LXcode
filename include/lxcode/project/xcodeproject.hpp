#pragma once
#include "lxcode/core/expected.hpp"
#include "lxcode/project/pbxproj.hpp"
#include "lxcode/project/scheme.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace lxcode {

struct PbxPhase {
    std::string id;
    std::string isa;
    std::string name;
    std::string shellScript;
    std::vector<std::string> buildFiles;
    std::vector<std::string> fileRefs;
};

struct PbxTargetInfo {
    std::string id;
    std::string isa;
    std::string name;
    std::string productName;
    std::string productReference;
    std::string productType;
    std::string configurationList;
    std::vector<std::string> buildPhases;
    std::vector<std::string> dependencies;
    std::vector<PbxPhase> phases;
};

struct PbxFileInfo {
    std::string id;
    std::string name;
    std::string path;
    std::string sourceTree;
    std::string fileType;
};

struct NavigatorItem {
    std::string id;
    std::string name;
    std::string path;
    std::string kind;
    std::string fileRefId;
    bool group = false;
    std::vector<NavigatorItem> children;
};

class XcodeProject {
public:
    static Expected<XcodeProject> open(const std::filesystem::path& path);
    static Expected<XcodeProject> create(const std::filesystem::path& directory,
                                         const std::string& name,
                                         const std::string& productType =
                                             "com.apple.product-type.tool");

    std::filesystem::path path() const { return path_; }
    std::filesystem::path directory() const { return path_.parent_path(); }
    // The directory containing the .xcodeproj bundle (Xcode's SRCROOT).
    std::filesystem::path sourceRoot() const { return directory().parent_path(); }
    std::string name() const;
    pbx::PbxDocument& document() { return document_; }
    const pbx::PbxDocument& document() const { return document_; }

    Expected<void> save() const;

    std::vector<PbxTargetInfo> targets() const;
    std::vector<PbxFileInfo> files() const;
    std::vector<NavigatorItem> navigator() const;
    std::vector<SchemeDocument> schemes() const;
    std::map<std::string, std::string> buildSettings(const std::string& targetId,
                                                     const std::string& configuration) const;
    std::filesystem::path resolvedPath(const std::string& fileRefId) const;
    std::string fileType(const std::string& path) const;

    Expected<std::string> addFile(const std::filesystem::path& path,
                                  const std::string& groupId = {},
                                  const std::string& targetId = {});
    Expected<void> removeFile(const std::string& fileRefId);
    Expected<void> renameFile(const std::string& fileRefId, const std::string& newName,
                              bool renameOnDisk = true);
    Expected<void> addGroup(const std::string& name, const std::string& parentGroupId = {});
    Expected<void> setBuildSetting(const std::string& targetId, const std::string& configuration,
                                   const std::string& key, const std::string& value);

    std::vector<std::string> diagnostics() const;

private:
    std::filesystem::path path_;
    pbx::PbxDocument document_;

    NavigatorItem buildNavigatorItem(const std::string& id, std::string parentPath,
                                     int depth) const;
    std::string findGroupForFile(const std::string& fileRefId) const;
};

} // namespace lxcode
