#pragma once
#include "lxcode/core/expected.hpp"

#include <string>
#include <utility>
#include <vector>

namespace lxcode::xml {

struct Attribute {
    std::string name;
    std::string value;
};

struct Node {
    std::string name;
    std::vector<Attribute> attributes;
    std::string text;
    std::vector<Node> children;
    bool selfClosing = false;

    const Attribute* attribute(const std::string& name) const;
    std::string attributeValue(const std::string& name, std::string fallback = {}) const;
    void setAttribute(const std::string& name, std::string value);
    const Node* child(const std::string& name) const;
    Node* child(const std::string& name);
    std::vector<const Node*> childrenNamed(const std::string& name) const;
    std::string childText(const std::string& name, std::string fallback = {}) const;
};

class Document {
public:
    Expected<void> parse(std::string text);
    std::string serialize() const;

    Node& root() { return root_; }
    const Node& root() const { return root_; }
    std::vector<std::string>& prolog() { return prolog_; }

    static std::string escape(std::string_view text);
    static std::string unescape(std::string_view text);

private:
    Node root_;
    std::vector<std::string> prolog_;
};

} // namespace lxcode::xml
