#pragma once
#include "lxcode/core/expected.hpp"
#include "lxcode/project/xml.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace lxcode {

struct WorkspaceFileRef {
    std::string location;
    std::string name;
};

class XcWorkspace {
public:
    static Expected<XcWorkspace> open(const std::filesystem::path& path);
    static Expected<XcWorkspace> create(const std::filesystem::path& directory,
                                        const std::string& name);

    std::filesystem::path path() const { return path_; }
    std::filesystem::path directory() const { return path_.parent_path(); }
    std::vector<WorkspaceFileRef> fileReferences() const;
    std::vector<std::filesystem::path> projectPaths() const;
    std::vector<std::string> schemes() const;
    Expected<void> save() const;

private:
    std::filesystem::path path_;
    xml::Document document_;
};

} // namespace lxcode
