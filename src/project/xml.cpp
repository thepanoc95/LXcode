#include "lxcode/project/xml.hpp"

#include "lxcode/core/strings.hpp"

#include <cctype>
#include <sstream>

namespace lxcode::xml {

namespace {

std::string parseName(const std::string& text, std::size_t& i) {
    const auto start = i;
    while (i < text.size()) {
        const char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c)) || c == '/' || c == '>' || c == '=') break;
        ++i;
    }
    return text.substr(start, i - start);
}

std::string parseQuoted(const std::string& text, std::size_t& i, std::string& error) {
    if (i >= text.size() || (text[i] != '"' && text[i] != '\'')) {
        error = "expected quoted attribute value";
        return {};
    }
    const char quote = text[i++];
    const auto start = i;
    while (i < text.size() && text[i] != quote) ++i;
    if (i >= text.size()) {
        error = "unterminated attribute value";
        return {};
    }
    auto value = text.substr(start, i - start);
    ++i;
    return Document::unescape(value);
}

bool skipSpace(const std::string& text, std::size_t& i) {
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    return i < text.size();
}

class XmlParser {
public:
    explicit XmlParser(std::string text) : text_(std::move(text)) {}

    bool parse(Node& root, std::vector<std::string>& prolog, std::string& error) {
        while (i_ < text_.size()) {
            if (text_[i_] != '<') {
                ++i_;
                continue;
            }
            if (strings::startsWith(std::string_view(text_).substr(i_), "<?")) {
                const auto end = text_.find("?>", i_ + 2);
                if (end == std::string::npos) { error = "unterminated XML declaration"; return false; }
                prolog.push_back(text_.substr(i_, end + 2 - i_));
                i_ = end + 2;
                continue;
            }
            if (strings::startsWith(std::string_view(text_).substr(i_), "<!--")) {
                const auto end = text_.find("-->", i_ + 4);
                if (end == std::string::npos) { error = "unterminated XML comment"; return false; }
                Node comment;
                comment.name = "#comment";
                comment.text = text_.substr(i_ + 4, end - i_ - 4);
                root.children.push_back(std::move(comment));
                i_ = end + 3;
                continue;
            }
            if (strings::startsWith(std::string_view(text_).substr(i_), "<!")) {
                const auto end = text_.find('>', i_ + 2);
                if (end == std::string::npos) { error = "unterminated XML declaration"; return false; }
                prolog.push_back(text_.substr(i_, end + 1 - i_));
                i_ = end + 1;
                continue;
            }
            root = parseElement(error);
            return error.empty();
        }
        error = "no root element";
        return false;
    }

private:
    Node parseElement(std::string& error) {
        Node node;
        ++i_; // <
        node.name = parseName(text_, i_);
        if (node.name.empty()) { error = "missing element name"; return node; }
        while (skipSpace(text_, i_)) {
            if (text_[i_] == '>') { ++i_; break; }
            if (text_[i_] == '/') {
                if (i_ + 1 >= text_.size() || text_[i_ + 1] != '>') {
                    error = "malformed self-closing element";
                    return node;
                }
                node.selfClosing = true;
                i_ += 2;
                break;
            }
            auto name = parseName(text_, i_);
            skipSpace(text_, i_);
            if (i_ >= text_.size() || text_[i_] != '=') {
                error = "expected '=' after attribute " + name;
                return node;
            }
            ++i_;
            skipSpace(text_, i_);
            node.attributes.push_back({name, parseQuoted(text_, i_, error)});
            if (!error.empty()) return node;
        }
        if (node.selfClosing) return node;

        std::ostringstream text;
        while (i_ < text_.size()) {
            if (text_[i_] == '<') {
                if (strings::startsWith(std::string_view(text_).substr(i_), "<!--")) {
                    const auto end = text_.find("-->", i_ + 4);
                    if (end == std::string::npos) { error = "unterminated XML comment"; return node; }
                    Node comment;
                    comment.name = "#comment";
                    comment.text = text_.substr(i_ + 4, end - i_ - 4);
                    node.children.push_back(std::move(comment));
                    i_ = end + 3;
                    continue;
                }
                if (strings::startsWith(std::string_view(text_).substr(i_), "<![CDATA[")) {
                    const auto end = text_.find("]]>", i_ + 9);
                    if (end == std::string::npos) { error = "unterminated CDATA"; return node; }
                    text.write(text_.data() + i_ + 9, static_cast<std::streamsize>(end - i_ - 9));
                    i_ = end + 3;
                    continue;
                }
                if (i_ + 1 < text_.size() && text_[i_ + 1] == '/') {
                    i_ += 2;
                    auto closing = parseName(text_, i_);
                    skipSpace(text_, i_);
                    if (i_ < text_.size() && text_[i_] == '>') ++i_;
                    if (closing != node.name) error = "mismatched closing tag '" + closing + "'";
                    break;
                }
                node.children.push_back(parseElement(error));
                if (!error.empty()) return node;
                continue;
            }
            text.put(text_[i_++]);
        }
        node.text = Document::unescape(text.str());
        return node;
    }

    std::string text_;
    std::size_t i_ = 0;
};

void writeNode(const Node& node, int indent, std::string& out) {
    const std::string pad(static_cast<std::size_t>(indent), '\t');
    out.append(pad).push_back('<');
    out.append(node.name);
    for (const auto& attr : node.attributes) {
        out.push_back(' ');
        out.append(attr.name);
        out.append("=\"");
        out.append(Document::escape(attr.value));
        out.push_back('"');
    }
    if (node.selfClosing) {
        out.append("/>\n");
        return;
    }
    out.push_back('>');
    if (node.name == "#comment") {
        out.append(node.text).append("-->\n");
        return;
    }
    if (!node.text.empty()) out.append(Document::escape(node.text));
    if (!node.children.empty()) {
        out.push_back('\n');
        for (const auto& child : node.children) writeNode(child, indent + 1, out);
        out.append(pad);
    }
    out.append("</").append(node.name).append(">\n");
}

} // namespace

const Attribute* Node::attribute(const std::string& name) const {
    for (const auto& attr : attributes)
        if (attr.name == name) return &attr;
    return nullptr;
}

std::string Node::attributeValue(const std::string& name, std::string fallback) const {
    const auto* attr = attribute(name);
    return attr ? attr->value : std::move(fallback);
}

void Node::setAttribute(const std::string& name, std::string value) {
    for (auto& attr : attributes) {
        if (attr.name == name) { attr.value = std::move(value); return; }
    }
    attributes.push_back({name, std::move(value)});
}

const Node* Node::child(const std::string& name) const {
    for (const auto& item : children)
        if (item.name == name) return &item;
    return nullptr;
}

Node* Node::child(const std::string& name) {
    for (auto& item : children)
        if (item.name == name) return &item;
    return nullptr;
}

std::vector<const Node*> Node::childrenNamed(const std::string& name) const {
    std::vector<const Node*> result;
    for (const auto& item : children)
        if (item.name == name) result.push_back(&item);
    return result;
}

std::string Node::childText(const std::string& name, std::string fallback) const {
    const auto* node = child(name);
    return node ? node->text : std::move(fallback);
}

Expected<void> Document::parse(std::string text) {
    XmlParser parser(std::move(text));
    Node root;
    std::vector<std::string> prolog;
    std::string error;
    if (!parser.parse(root, prolog, error)) return Expected<void>::failure(error);
    root_ = std::move(root);
    prolog_ = std::move(prolog);
    return {};
}

std::string Document::serialize() const {
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    for (const auto& item : prolog_) {
        if (strings::startsWith(item, "<?xml")) continue;
        out.append(item).push_back('\n');
    }
    writeNode(root_, 0, out);
    return out;
}

std::string Document::escape(std::string_view text) {
    return strings::replaceAll(strings::replaceAll(
        strings::replaceAll(strings::replaceAll(strings::replaceAll(
            std::string(text), "&", "&amp;"), "<", "&lt;"), ">", "&gt;"), "\"", "&quot;"), "'", "&apos;");
}

std::string Document::unescape(std::string_view text) {
    std::string result(text);
    // Decode numeric entities first so sequences such as &#38;amp; become &amp;.
    for (std::size_t i = 0; i + 2 < result.size();) {
        if (result[i] != '&' || result[i + 1] != '#') { ++i; continue; }
        const auto end = result.find(';', i + 2);
        if (end == std::string::npos) break;
        const auto digits = result.substr(i + 2, end - i - 2);
        try {
            const auto code = std::stoul(digits, nullptr, digits.size() > 1 && digits[0] == 'x' ? 16 : 10);
            if (code <= 0x7f) result.replace(i, end + 1 - i, 1, static_cast<char>(code));
            else result.replace(i, end + 1 - i, "?");
        } catch (...) { ++i; }
    }
    return strings::replaceAll(strings::replaceAll(
        strings::replaceAll(strings::replaceAll(strings::replaceAll(
            result, "&amp;", "&"), "&lt;", "<"), "&gt;", ">"), "&quot;", "\""), "&apos;", "'");
}

} // namespace lxcode::xml
