#pragma once
#include "lxcode/core/expected.hpp"

#include <string>
#include <utility>
#include <vector>

namespace lxcode::plist {

struct Value {
    enum class Kind { Dictionary, Array, String, Integer, Real, Boolean, Data, Date };

    Kind kind = Kind::String;
    std::vector<std::pair<std::string, Value>> dictionary;
    std::vector<Value> array;
    std::string text;
    bool boolean = false;

    static Value dict();
    static Value arr();
    static Value string(std::string value);
    static Value integer(std::string value);
    static Value real(std::string value);
    static Value booleanValue(bool value);

    const Value* find(const std::string& key) const;
    Value* find(const std::string& key);
    std::string stringValue(const std::string& key, std::string fallback = {}) const;
    bool set(const std::string& key, Value value);
};

class Document {
public:
    Expected<void> parse(std::string text);
    std::string serialize() const;

    Value& root() { return root_; }
    const Value& root() const { return root_; }

    std::string valueAt(const std::string& dottedPath, std::string fallback = {}) const;
    bool setValueAt(const std::string& dottedPath, std::string value);

private:
    Value root_;
};

} // namespace lxcode::plist
