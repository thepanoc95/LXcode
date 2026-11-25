#pragma once
#include "lxcode/core/expected.hpp"

#include <string>
#include <utility>
#include <vector>

namespace lxcode::pbx {

// The OpenStep-style property list used by project.pbxproj. Values keep their
// original ordering, quoting and comments so that LXcode can modify one field
// without discarding metadata it does not understand.
struct PbxValue {
    enum class Kind { String, Dictionary, Array, Data };

    Kind kind = Kind::String;
    std::string string;
    bool quoted = false;
    std::vector<std::string> leadingComments;
    std::vector<std::string> trailingComments;

    std::vector<struct PbxEntry> dictionary;
    std::vector<PbxValue> array;
    std::string data;

    static PbxValue makeString(std::string value, bool quote = false);
    static PbxValue makeDictionary();
    static PbxValue makeArray();
    static PbxValue makeData(std::string value);

    const PbxValue* find(const std::string& key) const;
    PbxValue* find(const std::string& key);
    std::string stringValue(const std::string& key, std::string fallback = {}) const;
    std::vector<std::string> stringArray(const std::string& key) const;
    bool set(const std::string& key, PbxValue value);
    bool erase(const std::string& key);
};

struct PbxEntry {
    std::string key;
    bool quoted = false;
    PbxValue value;
    std::vector<std::string> leadingComments;
    std::vector<std::string> trailingComments;
};

class PbxDocument {
public:
    Expected<void> parse(std::string text);
    std::string serialize() const;
    bool isParsed() const { return parsed_; }

    PbxValue* object(const std::string& identifier);
    const PbxValue* object(const std::string& identifier) const;
    std::vector<std::string> objectIds() const;
    std::vector<std::string> objectIdsOfClass(const std::string& isa) const;

    std::string isa(const std::string& identifier) const;
    std::string fieldString(const std::string& identifier, const std::string& key,
                            std::string fallback = {}) const;
    std::vector<std::string> fieldArray(const std::string& identifier,
                                        const std::string& key) const;
    bool setField(const std::string& identifier, const std::string& key, PbxValue value);
    bool setString(const std::string& identifier, const std::string& key, std::string value,
                   bool quote = false);
    bool appendToArray(const std::string& identifier, const std::string& key, PbxValue value);

    // Returns a fresh, unique 24-character hexadecimal identifier.
    std::string makeIdentifier(const std::string& hint = {});

    std::string addObject(std::string isa, std::vector<PbxEntry> fields = {});
    bool removeObject(const std::string& identifier);

    // Removes dangling object references from arrays/dictionaries throughout
    // the object section. Unknown keys are still preserved.
    int pruneReferences(const std::string& identifier);

    std::vector<std::string> validate() const;
    std::string rootObjectId() const;

    PbxValue& root() { return root_; }
    const PbxValue& root() const { return root_; }

private:
    PbxValue root_;
    bool parsed_ = false;
};

} // namespace lxcode::pbx
