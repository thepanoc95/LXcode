#pragma once
#include "lxcode/core/expected.hpp"
#include "lxcode/project/xml.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace lxcode {

struct BuildableReference {
    std::string name;
    std::string blueprintIdentifier;
    std::string buildableName;
    std::string blueprintName;
    std::string referencedContainer;
    std::string targetIdentifier;
};

class SchemeDocument {
public:
    static Expected<SchemeDocument> open(const std::filesystem::path& path);
    static SchemeDocument create(const std::string& name, const BuildableReference& reference,
                                 const std::string& configuration = "Debug");

    std::string name() const;
    std::filesystem::path path() const { return path_; }
    void setPath(std::filesystem::path path) { path_ = std::move(path); }
    std::vector<BuildableReference> buildableReferences() const;
    std::vector<BuildableReference> buildEntries(const std::string& actionName) const;
    std::string buildConfiguration(const std::string& actionName) const;
    void setBuildConfiguration(const std::string& actionName, std::string configuration);
    xml::Document& document() { return document_; }
    Expected<void> save() const;

private:
    std::filesystem::path path_;
    xml::Document document_;
};

BuildableReference buildableFromNode(const xml::Node& node);

} // namespace lxcode
