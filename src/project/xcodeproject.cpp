#include "lxcode/project/xcodeproject.hpp"

#include "lxcode/core/filesystem.hpp"
#include "lxcode/core/strings.hpp"
#include "lxcode/project/xcconfig.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace lxcode {

namespace {

const char* kProjectTemplate = R"PBXPROJ(// !$*UTF8*$!
{
	archiveVersion = 1;
	classes = {
	};
	objectVersion = 56;
	objects = {

/* Begin PBXBuildFile section */
		%MAIN_BUILD_FILE% /* main.cpp in Sources */ = {isa = PBXBuildFile; fileRef = %MAIN_FILE_REF% /* main.cpp */; };
/* End PBXBuildFile section */

/* Begin PBXFileReference section */
		%MAIN_FILE_REF% /* main.cpp */ = {isa = PBXFileReference; lastKnownFileType = sourcecode.cpp.cpp; path = main.cpp; sourceTree = "<group>"; };
		%PRODUCT_REF% /* lxcode_product */ = {isa = PBXFileReference; explicitFileType = "compiled.mach-o.executable"; includeInIndex = 0; path = lxcode_product; sourceTree = BUILT_PRODUCTS_DIR; };
/* End PBXFileReference section */

/* Begin PBXFrameworksBuildPhase section */
		%FRAMEWORKS_PHASE% /* Frameworks */ = {
			isa = PBXFrameworksBuildPhase;
			buildActionMask = 2147483647;
			files = (
			);
			runOnlyForDeploymentPostprocessing = 0;
		};
/* End PBXFrameworksBuildPhase section */

/* Begin PBXGroup section */
		%MAIN_GROUP% = {
			isa = PBXGroup;
			children = (
				%MAIN_FILE_REF% /* main.cpp */,
				%PRODUCTS_GROUP% /* Products */,
			);
			sourceTree = "<group>";
		};
		%PRODUCTS_GROUP% /* Products */ = {
			isa = PBXGroup;
			children = (
				%PRODUCT_REF% /* lxcode_product */,
			);
			name = Products;
			sourceTree = "<group>";
		};
/* End PBXGroup section */

/* Begin PBXNativeTarget section */
		%TARGET% /* lxcode_product */ = {
			isa = PBXNativeTarget;
			buildConfigurationList = %TARGET_CONFIG_LIST% /* Build configuration list for PBXNativeTarget "lxcode_product" */;
			buildPhases = (
				%SOURCES_PHASE% /* Sources */,
				%FRAMEWORKS_PHASE% /* Frameworks */,
				%RESOURCES_PHASE% /* Resources */,
			);
			buildRules = (
			);
			dependencies = (
			);
			name = lxcode_product;
			productName = lxcode_product;
			productReference = %PRODUCT_REF% /* lxcode_product */;
			productType = "com.apple.product-type.tool";
		};
/* End PBXNativeTarget section */

/* Begin PBXProject section */
		%PROJECT% /* Project object */ = {
			isa = PBXProject;
			attributes = {
				BuildIndependentTargetsInParallel = 1;
				LastUpgradeCheck = 1600;
			};
			buildConfigurationList = %PROJECT_CONFIG_LIST% /* Build configuration list for PBXProject "lxcode_product" */;
			compatibilityVersion = "Xcode 14.0";
			developmentRegion = en;
			hasScannedForEncodings = 0;
			knownRegions = (
				en,
				Base,
			);
			mainGroup = %MAIN_GROUP%;
			productRefGroup = %PRODUCTS_GROUP% /* Products */;
			projectDirPath = "";
			projectRoot = "";
			targets = (
				%TARGET% /* lxcode_product */,
			);
		};
/* End PBXProject section */

/* Begin PBXResourcesBuildPhase section */
		%RESOURCES_PHASE% /* Resources */ = {
			isa = PBXResourcesBuildPhase;
			buildActionMask = 2147483647;
			files = (
			);
			runOnlyForDeploymentPostprocessing = 0;
		};
/* End PBXResourcesBuildPhase section */

/* Begin PBXSourcesBuildPhase section */
		%SOURCES_PHASE% /* Sources */ = {
			isa = PBXSourcesBuildPhase;
			buildActionMask = 2147483647;
			files = (
				%MAIN_BUILD_FILE% /* main.cpp in Sources */,
			);
			runOnlyForDeploymentPostprocessing = 0;
		};
/* End PBXSourcesBuildPhase section */

/* Begin XCBuildConfiguration section */
		%PROJECT_DEBUG% /* Debug */ = {
			isa = XCBuildConfiguration;
			buildSettings = {
				ALWAYS_SEARCH_USER_PATHS = NO;
				CLANG_CXX_LANGUAGE_STANDARD = "c++20";
				CLANG_ENABLE_MODULES = YES;
				CLANG_ENABLE_OBJC_ARC = YES;
				CODE_SIGNING_ALLOWED = NO;
				DEBUG_INFORMATION_FORMAT = dwarf;
				ENABLE_TESTABILITY = YES;
				GCC_OPTIMIZATION_LEVEL = 0;
				GCC_PREPROCESSOR_DEFINITIONS = (
					"DEBUG=1",
					"$(inherited)",
				);
				MACOSX_DEPLOYMENT_TARGET = 11.0;
				ONLY_ACTIVE_ARCH = YES;
				SDKROOT = macosx;
				SWIFT_OPTIMIZATION_LEVEL = "-Onone";
			};
			name = Debug;
		};
		%PROJECT_RELEASE% /* Release */ = {
			isa = XCBuildConfiguration;
			buildSettings = {
				ALWAYS_SEARCH_USER_PATHS = NO;
				CLANG_CXX_LANGUAGE_STANDARD = "c++20";
				CLANG_ENABLE_MODULES = YES;
				CLANG_ENABLE_OBJC_ARC = YES;
				CODE_SIGNING_ALLOWED = NO;
				DEBUG_INFORMATION_FORMAT = "dwarf-with-dsym";
				MACOSX_DEPLOYMENT_TARGET = 11.0;
				SDKROOT = macosx;
				SWIFT_COMPILATION_MODE = wholemodule;
				SWIFT_OPTIMIZATION_LEVEL = "-O";
			};
			name = Release;
		};
		%TARGET_DEBUG% /* Debug */ = {
			isa = XCBuildConfiguration;
			buildSettings = {
				PRODUCT_NAME = "$(TARGET_NAME)";
			};
			name = Debug;
		};
		%TARGET_RELEASE% /* Release */ = {
			isa = XCBuildConfiguration;
			buildSettings = {
				PRODUCT_NAME = "$(TARGET_NAME)";
			};
			name = Release;
		};
/* End XCBuildConfiguration section */

/* Begin XCConfigurationList section */
		%PROJECT_CONFIG_LIST% /* Build configuration list for PBXProject "lxcode_product" */ = {
			isa = XCConfigurationList;
			buildConfigurations = (
				%PROJECT_DEBUG% /* Debug */,
				%PROJECT_RELEASE% /* Release */,
			);
			defaultConfigurationIsVisible = 0;
			defaultConfigurationName = Release;
		};
		%TARGET_CONFIG_LIST% /* Build configuration list for PBXNativeTarget "lxcode_product" */ = {
			isa = XCConfigurationList;
			buildConfigurations = (
				%TARGET_DEBUG% /* Debug */,
				%TARGET_RELEASE% /* Release */,
			);
			defaultConfigurationIsVisible = 0;
			defaultConfigurationName = Release;
		};
/* End XCConfigurationList section */
	};
	rootObject = %PROJECT% /* Project object */;
}
)PBXPROJ";

std::string configIdFor(const pbx::PbxDocument& document, const std::string& targetId) {
    if (targetId.empty()) return document.root().stringValue("rootObject");
    return document.fieldString(targetId, "buildConfigurationList");
}

} // namespace

Expected<XcodeProject> XcodeProject::open(const std::filesystem::path& path) {
    XcodeProject result;
    std::error_code ec;
    result.path_ = path;
    if (std::filesystem::is_directory(path, ec)) {
        result.path_ = path / "project.pbxproj";
    } else if (path.filename() == "project.pbxproj") {
        result.path_ = path;
    } else {
        result.path_ = path / "project.pbxproj";
    }
    if (!fs::exists(result.path_))
        return Expected<XcodeProject>::failure("project file not found: " + result.path_.string());
    auto text = fs::readText(result.path_);
    if (!text) return Expected<XcodeProject>::failure(text.error());
    auto parsed = result.document_.parse(*text);
    if (!parsed) return Expected<XcodeProject>::failure(parsed.error());
    auto issues = result.document_.validate();
    if (result.document_.rootObjectId().empty())
        return Expected<XcodeProject>::failure("project.pbxproj has no rootObject");
    return result;
}

Expected<XcodeProject> XcodeProject::create(const std::filesystem::path& directory,
                                            const std::string& name,
                                            const std::string& productType) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    XcodeProject result;
    result.path_ = directory / "project.pbxproj";

    std::set<std::string> used;
    std::map<std::string, std::string> ids;
    for (const char* key : {"PROJECT", "MAIN_GROUP", "PRODUCTS_GROUP", "PRODUCT_REF",
                            "MAIN_FILE_REF", "MAIN_BUILD_FILE", "TARGET", "SOURCES_PHASE",
                            "FRAMEWORKS_PHASE", "RESOURCES_PHASE", "PROJECT_CONFIG_LIST",
                            "TARGET_CONFIG_LIST", "PROJECT_DEBUG", "PROJECT_RELEASE",
                            "TARGET_DEBUG", "TARGET_RELEASE"}) {
        std::string id;
        do { id = result.document_.makeIdentifier(); } while (!used.insert(id).second);
        ids[key] = id;
    }
    std::string contents = kProjectTemplate;
    for (const auto& [key, id] : ids)
        contents = strings::replaceAll(contents, "%" + key + "%", id);
    contents = strings::replaceAll(contents, "lxcode_product", name);
    contents = strings::replaceAll(contents, "com.apple.product-type.tool", productType);

    auto parsed = result.document_.parse(contents);
    if (!parsed) return Expected<XcodeProject>::failure(parsed.error());

    auto saved = result.save();
    if (!saved) return Expected<XcodeProject>::failure(saved.error());

    std::ofstream mainFile(result.sourceRoot() / "main.cpp", std::ios::binary | std::ios::trunc);
    if (mainFile) {
        mainFile << "// Created by LXcode. Builds with a configured macOS cross toolchain.\n"
                 << "#include <cstdio>\n\n"
                 << "int main(int argc, char** argv) {\n"
                 << "    std::printf(\"Hello from " << name << "!\\n\");\n"
                 << "    return 0;\n}\n";
    }

    BuildableReference reference;
    reference.blueprintIdentifier = ids["TARGET"];
    reference.buildableName = name;
    reference.blueprintName = name;
    reference.referencedContainer = "container:" + name + ".xcodeproj";
    auto scheme = SchemeDocument::create(name, reference, "Debug");
    auto schemesDir = directory / "xcshareddata" / "xcschemes";
    std::filesystem::create_directories(schemesDir, ec);
    scheme.setPath(schemesDir / (name + ".xcscheme"));
    auto schemeSaved = scheme.save();
    if (!schemeSaved) return Expected<XcodeProject>::failure(schemeSaved.error());
    return result;
}

std::string XcodeProject::name() const {
    auto root = document_.rootObjectId();
    const auto* group = document_.object(document_.fieldString(root, "mainGroup"));
    if (group && !group->stringValue("name").empty()) return group->stringValue("name");
    return path_.parent_path().stem().string();
}

Expected<void> XcodeProject::save() const {
    auto issues = document_.validate();
    if (!issues.empty()) return Expected<void>::failure("project validation failed: " + issues.front());
    return fs::writeTextAtomic(path_, document_.serialize());
}

PbxPhase makePhase(const pbx::PbxDocument& document, const std::string& id) {
    PbxPhase phase;
    phase.id = id;
    phase.isa = document.isa(id);
    phase.name = document.fieldString(id, "name");
    phase.shellScript = document.fieldString(id, "shellScript");
    phase.buildFiles = document.fieldArray(id, "files");
    for (const auto& buildFile : phase.buildFiles)
        phase.fileRefs.push_back(document.fieldString(buildFile, "fileRef"));
    return phase;
}

std::vector<PbxTargetInfo> XcodeProject::targets() const {
    std::vector<PbxTargetInfo> result;
    auto ids = document_.objectIdsOfClass("PBXNativeTarget");
    auto aggregate = document_.objectIdsOfClass("PBXAggregateTarget");
    ids.insert(ids.end(), aggregate.begin(), aggregate.end());
    for (const auto& id : ids) {
        PbxTargetInfo target;
        target.id = id;
        target.isa = document_.isa(id);
        target.name = document_.fieldString(id, "name");
        target.productName = document_.fieldString(id, "productName", target.name);
        target.productReference = document_.fieldString(id, "productReference");
        target.productType = document_.fieldString(id, "productType");
        target.configurationList = document_.fieldString(id, "buildConfigurationList");
        target.buildPhases = document_.fieldArray(id, "buildPhases");
        target.dependencies = document_.fieldArray(id, "dependencies");
        for (const auto& phaseId : target.buildPhases)
            target.phases.push_back(makePhase(document_, phaseId));
        if (target.name.empty()) target.name = target.productName;
        result.push_back(std::move(target));
    }
    return result;
}

std::vector<PbxFileInfo> XcodeProject::files() const {
    std::vector<PbxFileInfo> result;
    for (const auto& id : document_.objectIdsOfClass("PBXFileReference")) {
        PbxFileInfo file;
        file.id = id;
        file.name = document_.fieldString(id, "name");
        file.path = document_.fieldString(id, "path");
        file.sourceTree = document_.fieldString(id, "sourceTree", "<group>");
        file.fileType = document_.fieldString(id, "lastKnownFileType",
                                              document_.fieldString(id, "explicitFileType"));
        if (file.name.empty()) file.name = std::filesystem::path(file.path).filename().string();
        result.push_back(std::move(file));
    }
    return result;
}

NavigatorItem XcodeProject::buildNavigatorItem(const std::string& id, std::string parentPath,
                                               int depth) const {
    NavigatorItem item;
    item.id = id;
    if (depth > 64) return item;
    const auto isa = document_.isa(id);
    const bool group = isa.find("Group") != std::string::npos;
    const auto path = document_.fieldString(id, "path");
    const auto name = document_.fieldString(id, "name");
    item.group = group;
    item.kind = group ? "group" : isa;
    item.fileRefId = isa == "PBXFileReference" ? id : std::string();
    if (!name.empty()) item.name = name;
    else if (!path.empty()) item.name = std::filesystem::path(path).filename().string();
    else item.name = isa;
    item.path = path.empty() ? parentPath : (parentPath.empty() ? path : parentPath + "/" + path);

    if (group) {
        std::string childParent = item.path;
        if (document_.fieldString(id, "sourceTree") == "SOURCE_ROOT") childParent.clear();
        if (childParent == "<group>" || childParent == ".") childParent.clear();
        for (const auto& child : document_.fieldArray(id, "children"))
            item.children.push_back(buildNavigatorItem(child, childParent, depth + 1));
    } else {
        item.kind = document_.fieldString(id, "lastKnownFileType",
                                          document_.fieldString(id, "explicitFileType", isa));
    }
    return item;
}

std::vector<NavigatorItem> XcodeProject::navigator() const {
    std::vector<NavigatorItem> result;
    const auto root = document_.rootObjectId();
    const auto mainGroup = document_.fieldString(root, "mainGroup");
    if (mainGroup.empty()) return result;
    result.push_back(buildNavigatorItem(mainGroup, {}, 0));
    result.front().name = name();
    result.front().kind = "project";
    return result;
}

std::vector<SchemeDocument> XcodeProject::schemes() const {
    std::vector<SchemeDocument> result;
    auto paths = fs::filesWithExtension(directory() / "xcshareddata" / "xcschemes", "xcscheme");
    auto userPaths = fs::filesWithExtension(directory() / "xcuserdata", "xcscheme");
    paths.insert(paths.end(), userPaths.begin(), userPaths.end());
    std::set<std::string> seen;
    for (const auto& path : paths) {
        if (!seen.insert(path.string()).second) continue;
        auto scheme = SchemeDocument::open(path);
        if (scheme) result.push_back(*scheme);
    }
    return result;
}

std::map<std::string, std::string> XcodeProject::buildSettings(const std::string& targetId,
                                                               const std::string& configuration) const {
    std::map<std::string, std::string> result;
    auto applyConfigList = [&](const std::string& listId) {
        for (const auto& configId : document_.fieldArray(listId, "buildConfigurations")) {
            if (document_.fieldString(configId, "name") != configuration) continue;
            if (const auto* settings = document_.object(configId)->find("buildSettings")) {
                for (const auto& entry : settings->dictionary) {
                    if (entry.value.kind == pbx::PbxValue::Kind::String)
                        result[entry.key] = entry.value.string;
                    else if (entry.value.kind == pbx::PbxValue::Kind::Array) {
                        std::vector<std::string> values;
                        for (const auto& item : entry.value.array)
                            if (item.kind == pbx::PbxValue::Kind::String) values.push_back(item.string);
                        result[entry.key] = strings::join(values, " ");
                    }
                }
            }
            const auto base = document_.fieldString(configId, "baseConfigurationReference");
            if (!base.empty()) {
                auto path = resolvedPath(base);
                auto text = fs::readText(path);
                if (text) {
                    XcconfigDocument xcconfig;
                    xcconfig.parse(*text);
                    for (const auto& key : xcconfig.settingNames()) result[key] = xcconfig.setting(key);
                }
            }
        }
    };
    applyConfigList(document_.fieldString(document_.rootObjectId(), "buildConfigurationList"));
    if (!targetId.empty()) applyConfigList(document_.fieldString(targetId, "buildConfigurationList"));
    result["TARGET_NAME"] = targetId.empty() ? name() : document_.fieldString(targetId, "name", name());
    return result;
}

std::filesystem::path XcodeProject::resolvedPath(const std::string& fileRefId) const {
    if (fileRefId.empty()) return {};
    const auto relative = document_.fieldString(fileRefId, "path");
    if (relative.empty()) return {};
    const auto sourceTree = document_.fieldString(fileRefId, "sourceTree", "<group>");
    if (sourceTree == "BUILT_PRODUCTS_DIR" || sourceTree == "DEVELOPER_DIR" ||
        sourceTree == "SDKROOT")
        return relative;
    return (sourceRoot() / relative).lexically_normal();
}

std::string XcodeProject::fileType(const std::string& path) const {
    const auto ext = strings::toLower(fs::extension(path));
    if (ext == "cpp" || ext == "cc" || ext == "cxx") return "sourcecode.cpp.cpp";
    if (ext == "c") return "sourcecode.c.c";
    if (ext == "m") return "sourcecode.c.objc";
    if (ext == "mm") return "sourcecode.cpp.objcpp";
    if (ext == "h" || ext == "hpp" || ext == "hh") return "sourcecode.c.h";
    if (ext == "swift") return "sourcecode.swift";
    if (ext == "plist") return "text.plist.xml";
    if (ext == "strings") return "text.plist.strings";
    if (ext == "xcassets") return "folder.assetcatalog";
    return "file";
}

Expected<std::string> XcodeProject::addFile(const std::filesystem::path& path,
                                            const std::string& groupId,
                                            const std::string& targetId) {
    if (!fs::exists(path)) return Expected<std::string>::failure("file does not exist: " + path.string());
    std::error_code ec;
    auto relative = std::filesystem::relative(path, sourceRoot(), ec).generic_string();
    if (ec || relative.find("..") == 0) relative = path.filename().string();
    const auto type = fileType(path);
    std::vector<pbx::PbxEntry> fields;
    fields.push_back({"lastKnownFileType", true, pbx::PbxValue::makeString(type, false), {}, {}});
    fields.push_back({"path", true, pbx::PbxValue::makeString(relative, false), {}, {}});
    fields.push_back({"sourceTree", false, pbx::PbxValue::makeString("SOURCEROOT_GROUP"), {}, {}});
    const auto fileRef = document_.addObject("PBXFileReference", std::move(fields));

    const auto mainGroup = document_.fieldString(document_.rootObjectId(), "mainGroup");
    const auto destinationGroup = groupId.empty() ? mainGroup : groupId;
    document_.appendToArray(destinationGroup, "children",
                            pbx::PbxValue::makeString(fileRef, false));

    if (!targetId.empty()) {
        bool source = strings::startsWith(type, "sourcecode") && type != "sourcecode.c.h" &&
                      type != "text.plist.strings";
        std::string phaseIsa = source ? "PBXSourcesBuildPhase" : "PBXResourcesBuildPhase";
        std::string phaseId;
        for (const auto& candidate : document_.fieldArray(targetId, "buildPhases"))
            if (document_.isa(candidate) == phaseIsa) { phaseId = candidate; break; }
        if (!phaseId.empty()) {
            std::vector<pbx::PbxEntry> buildFields;
            buildFields.push_back({"fileRef", false, pbx::PbxValue::makeString(fileRef), {}, {}});
            const auto buildFile = document_.addObject("PBXBuildFile", std::move(buildFields));
            document_.appendToArray(phaseId, "files", pbx::PbxValue::makeString(buildFile));
        }
    }
    return fileRef;
}

Expected<void> XcodeProject::removeFile(const std::string& fileRefId) {
    for (const auto& id : document_.objectIds()) {
        if (document_.isa(id) == "PBXBuildFile" &&
            document_.fieldString(id, "fileRef") == fileRefId)
            document_.removeObject(id);
    }
    for (const auto& id : document_.objectIds()) {
        const auto isa = document_.isa(id);
        if (isa.find("Group") == std::string::npos) continue;
        auto* children = document_.object(id)->find("children");
        if (!children || children->kind != pbx::PbxValue::Kind::Array) continue;
        children->array.erase(
            std::remove_if(children->array.begin(), children->array.end(), [&](const pbx::PbxValue& v) {
                return v.kind == pbx::PbxValue::Kind::String && v.string == fileRefId;
            }), children->array.end());
    }
    if (!document_.removeObject(fileRefId))
        return Expected<void>::failure("no such file reference: " + fileRefId);
    return {};
}

Expected<void> XcodeProject::renameFile(const std::string& fileRefId, const std::string& newName,
                                        bool renameOnDisk) {
    auto* fileRef = document_.object(fileRefId);
    if (!fileRef || document_.isa(fileRefId) != "PBXFileReference")
        return Expected<void>::failure("no such file reference: " + fileRefId);
    const auto oldPath = document_.fieldString(fileRefId, "path");
    if (renameOnDisk && !oldPath.empty()) {
        const auto oldFull = (sourceRoot() / oldPath).lexically_normal();
        const auto newFull = oldFull.parent_path() / newName;
        std::error_code ec;
        if (fs::exists(oldFull)) {
            std::filesystem::rename(oldFull, newFull, ec);
            if (ec) return Expected<void>::failure("cannot rename file: " + ec.message());
        }
    }
    document_.setString(fileRefId, "name", newName, true);
    if (!oldPath.empty())
        document_.setString(fileRefId, "path", newName, true);
    document_.setString(fileRefId, "lastKnownFileType", fileType(newName), false);
    return {};
}

Expected<void> XcodeProject::addGroup(const std::string& name, const std::string& parentGroupId) {
    std::vector<pbx::PbxEntry> fields;
    fields.push_back({"children", false, pbx::PbxValue::makeArray(), {}, {}});
    fields.push_back({"name", true, pbx::PbxValue::makeString(name, true), {}, {}});
    fields.push_back({"sourceTree", false, pbx::PbxValue::makeString("<group>"), {}, {}});
    const auto group = document_.addObject("PBXGroup", std::move(fields));
    const auto parent = parentGroupId.empty()
                            ? document_.fieldString(document_.rootObjectId(), "mainGroup")
                            : parentGroupId;
    document_.appendToArray(parent, "children", pbx::PbxValue::makeString(group));
    return {};
}

Expected<void> XcodeProject::setBuildSetting(const std::string& targetId,
                                             const std::string& configuration,
                                             const std::string& key, const std::string& value) {
    const auto listId = configIdFor(document_, targetId);
    if (listId.empty()) return Expected<void>::failure("no configuration list");
    for (const auto& id : document_.fieldArray(listId, "buildConfigurations")) {
        if (document_.fieldString(id, "name") != configuration) continue;
        auto* config = document_.object(id);
        auto* settings = config->find("buildSettings");
        if (!settings) {
            config->set("buildSettings", pbx::PbxValue::makeDictionary());
            settings = config->find("buildSettings");
        }
        settings->set(key, pbx::PbxValue::makeString(value, value.find(' ') != std::string::npos));
        return {};
    }
    return Expected<void>::failure("no configuration named " + configuration);
}

std::string XcodeProject::findGroupForFile(const std::string& fileRefId) const {
    for (const auto& id : document_.objectIds()) {
        if (document_.isa(id).find("Group") == std::string::npos) continue;
        const auto children = document_.fieldArray(id, "children");
        if (std::find(children.begin(), children.end(), fileRefId) != children.end()) return id;
    }
    return {};
}

std::vector<std::string> XcodeProject::diagnostics() const {
    auto issues = document_.validate();
    const auto root = document_.rootObjectId();
    for (const auto& target : targets()) {
        if (target.productReference.empty() || !document_.object(target.productReference))
            issues.push_back("target " + target.name + " has no valid product reference");
        if (target.configurationList.empty())
            issues.push_back("target " + target.name + " has no configuration list");
        for (const auto& phase : target.phases)
            for (const auto& fileRef : phase.fileRefs)
                if (!document_.object(fileRef))
                    issues.push_back("build phase " + phase.id + " references a missing file");
        for (const auto& dependency : target.dependencies) {
            const auto proxy = document_.fieldString(dependency, "container");
            if (!document_.object(proxy))
                issues.push_back("target " + target.name + " has a broken dependency");
        }
    }
    for (const auto& file : files()) {
        if (file.sourceTree == "SOURCE_ROOT" && !fs::exists(sourceRoot() / file.path))
            issues.push_back("missing file: " + file.path);
    }
    if (root.empty()) issues.push_back("missing root object");
    return issues;
}

} // namespace lxcode
