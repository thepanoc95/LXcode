#include "lxcode/project/plist.hpp"

#include "lxcode/project/pbxproj.hpp"
#include "lxcode/project/xml.hpp"

#include <cctype>

namespace lxcode::plist {

namespace {

Value fromPbx(const pbx::PbxValue& input) {
    switch (input.kind) {
    case pbx::PbxValue::Kind::String: return Value::string(input.string);
    case pbx::PbxValue::Kind::Data: {
        Value value;
        value.kind = Value::Kind::Data;
        value.text = input.data;
        return value;
    }
    case pbx::PbxValue::Kind::Array: {
        Value value = Value::arr();
        for (const auto& item : input.array) value.array.push_back(fromPbx(item));
        return value;
    }
    case pbx::PbxValue::Kind::Dictionary: {
        Value value = Value::dict();
        for (const auto& entry : input.dictionary)
            value.dictionary.emplace_back(entry.key, fromPbx(entry.value));
        return value;
    }
    }
    return Value::string({});
}

Value fromXml(const xml::Node& node) {
    if (node.name == "dict") {
        Value value = Value::dict();
        for (std::size_t i = 0; i < node.children.size();) {
            if (i + 1 >= node.children.size()) break;
            const auto& key = node.children[i];
            if (key.name == "#comment") { ++i; continue; }
            value.dictionary.emplace_back(key.text, fromXml(node.children[i + 1]));
            i += 2;
        }
        return value;
    }
    if (node.name == "array") {
        Value value = Value::arr();
        for (const auto& child : node.children)
            if (child.name != "#comment") value.array.push_back(fromXml(child));
        return value;
    }
    if (node.name == "true" || node.name == "false") return Value::booleanValue(node.name == "true");
    if (node.name == "integer") return Value::integer(node.text);
    if (node.name == "real") return Value::real(node.text);
    Value value = Value::string(node.text);
    if (node.name == "data") value.kind = Value::Kind::Data;
    if (node.name == "date") value.kind = Value::Kind::Date;
    return value;
}

xml::Node toXml(const std::string& name, const Value& value) {
    xml::Node node;
    node.name = name;
    switch (value.kind) {
    case Value::Kind::Dictionary:
        node.name = "dict";
        for (const auto& [key, child] : value.dictionary) {
            xml::Node keyNode;
            keyNode.name = "key";
            keyNode.text = key;
            node.children.push_back(keyNode);
            node.children.push_back(toXml("value", child));
        }
        break;
    case Value::Kind::Array:
        node.name = "array";
        for (const auto& child : value.array) node.children.push_back(toXml("value", child));
        break;
    case Value::Kind::String: node.name = "string"; node.text = value.text; break;
    case Value::Kind::Integer: node.name = "integer"; node.text = value.text; break;
    case Value::Kind::Real: node.name = "real"; node.text = value.text; break;
    case Value::Kind::Boolean:
        node.name = value.boolean ? "true" : "false";
        node.selfClosing = true;
        break;
    case Value::Kind::Data: node.name = "data"; node.text = value.text; break;
    case Value::Kind::Date: node.name = "date"; node.text = value.text; break;
    }
    return node;
}

} // namespace

Value Value::dict() { Value v; v.kind = Kind::Dictionary; return v; }
Value Value::arr() { Value v; v.kind = Kind::Array; return v; }
Value Value::string(std::string value) { Value v; v.text = std::move(value); return v; }
Value Value::integer(std::string value) {
    Value v; v.kind = Kind::Integer; v.text = std::move(value); return v;
}
Value Value::real(std::string value) {
    Value v; v.kind = Kind::Real; v.text = std::move(value); return v;
}
Value Value::booleanValue(bool value) {
    Value v; v.kind = Kind::Boolean; v.boolean = value; return v;
}

const Value* Value::find(const std::string& key) const {
    if (kind != Kind::Dictionary) return nullptr;
    for (const auto& [name, value] : dictionary)
        if (name == key) return &value;
    return nullptr;
}

Value* Value::find(const std::string& key) {
    if (kind != Kind::Dictionary) return nullptr;
    for (auto& [name, value] : dictionary)
        if (name == key) return &value;
    return nullptr;
}

std::string Value::stringValue(const std::string& key, std::string fallback) const {
    const auto* value = find(key);
    return value ? value->text : std::move(fallback);
}

bool Value::set(const std::string& key, Value value) {
    if (kind != Kind::Dictionary) return false;
    for (auto& [name, existing] : dictionary) {
        if (name == key) { existing = std::move(value); return true; }
    }
    dictionary.emplace_back(key, std::move(value));
    return true;
}

Expected<void> Document::parse(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first != std::string::npos && text[first] == '<') {
        xml::Document document;
        auto result = document.parse(text);
        if (!result) return Expected<void>::failure(result.error());
        const xml::Node* valueNode = nullptr;
        if (document.root().name == "plist") {
            for (const auto& child : document.root().children) {
                if (child.name == "#comment") continue;
                valueNode = &child;
                break;
            }
        } else if (const auto* plist = document.root().child("plist")) {
            for (const auto& child : plist->children) {
                if (child.name == "#comment") continue;
                valueNode = &child;
                break;
            }
        } else {
            valueNode = &document.root();
        }
        if (!valueNode) return Expected<void>::failure("plist has no value node");
        root_ = fromXml(*valueNode);
        return {};
    }
    pbx::PbxDocument ascii;
    auto result = ascii.parse(std::move(text));
    if (!result) return Expected<void>::failure(result.error());
    root_ = fromPbx(ascii.root());
    return {};
}

std::string Document::serialize() const {
    xml::Document document;
    document.prolog().push_back(
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">");
    xml::Node plist;
    plist.name = "plist";
    plist.attributes.push_back({"version", "1.0"});
    plist.children.push_back(toXml("root", root_));
    document.root() = plist;
    return document.serialize();
}

std::string Document::valueAt(const std::string& dottedPath, std::string fallback) const {
    const Value* current = &root_;
    std::size_t start = 0;
    while (true) {
        const auto dot = dottedPath.find('.', start);
        const auto part = dottedPath.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        current = current->find(part);
        if (!current) return fallback;
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return current->text.empty() ? fallback : current->text;
}

bool Document::setValueAt(const std::string& dottedPath, std::string value) {
    Value* current = &root_;
    std::size_t start = 0;
    while (true) {
        const auto dot = dottedPath.find('.', start);
        const auto part = dottedPath.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (dot == std::string::npos) return current->set(part, Value::string(std::move(value)));
        auto* next = current->find(part);
        if (!next) return false;
        current = next;
        start = dot + 1;
    }
}

} // namespace lxcode::plist
