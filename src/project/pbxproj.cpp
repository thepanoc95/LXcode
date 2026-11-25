#include "lxcode/project/pbxproj.hpp"

#include "lxcode/core/strings.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <random>
#include <sstream>

namespace lxcode::pbx {

namespace {

enum class TokenKind { LeftBrace, RightBrace, LeftParen, RightParen, Equal, Semicolon, Comma,
                       String, Data, Comment, End };

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;
    bool quoted = false;
};

std::vector<Token> lex(const std::string& input, std::string& error) {
    std::vector<Token> tokens;
    std::size_t i = 0;
    while (i < input.size()) {
        const char c = input[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < input.size() && input[i + 1] == '*') {
            const auto end = input.find("*/", i + 2);
            if (end == std::string::npos) {
                error = "unterminated block comment";
                return tokens;
            }
            tokens.push_back({TokenKind::Comment, input.substr(i, end + 2 - i), false});
            i = end + 2;
            continue;
        }
        if (c == '/' && i + 1 < input.size() && input[i + 1] == '/') {
            const auto end = input.find('\n', i + 2);
            const auto stop = end == std::string::npos ? input.size() : end;
            tokens.push_back({TokenKind::Comment, input.substr(i, stop - i), false});
            i = stop;
            continue;
        }
        switch (c) {
        case '{': tokens.push_back({TokenKind::LeftBrace, "{", false}); ++i; continue;
        case '}': tokens.push_back({TokenKind::RightBrace, "}", false}); ++i; continue;
        case '(': tokens.push_back({TokenKind::LeftParen, "(", false}); ++i; continue;
        case ')': tokens.push_back({TokenKind::RightParen, ")", false}); ++i; continue;
        case '=': tokens.push_back({TokenKind::Equal, "=", false}); ++i; continue;
        case ';': tokens.push_back({TokenKind::Semicolon, ";", false}); ++i; continue;
        case ',': tokens.push_back({TokenKind::Comma, ",", false}); ++i; continue;
        default: break;
        }
        if (c == '<') {
            const auto end = input.find('>', i + 1);
            if (end == std::string::npos) {
                error = "unterminated data literal";
                return tokens;
            }
            tokens.push_back({TokenKind::Data, input.substr(i + 1, end - i - 1), false});
            i = end + 1;
            continue;
        }
        if (c == '"') {
            std::string value;
            ++i;
            bool closed = false;
            while (i < input.size()) {
                const char c2 = input[i++];
                if (c2 == '"') { closed = true; break; }
                if (c2 == '\\' && i < input.size()) {
                    const char escaped = input[i++];
                    switch (escaped) {
                    case 'n': value.push_back('\n'); break;
                    case 'r': value.push_back('\r'); break;
                    case 't': value.push_back('\t'); break;
                    case 'U': {
                        if (i + 4 > input.size()) break;
                        const auto hex = input.substr(i, 4);
                        i += 4;
                        try {
                            const auto cp = std::stoul(hex, nullptr, 16);
                            if (cp <= 0x7f) value.push_back(static_cast<char>(cp));
                            else value.append("?"); // non-ASCII kept as-is by the caller
                        } catch (...) { value.append(hex); }
                        break;
                    }
                    default: value.push_back(escaped); break;
                    }
                } else {
                    value.push_back(c2);
                }
            }
            if (!closed) {
                error = "unterminated quoted string";
                return tokens;
            }
            tokens.push_back({TokenKind::String, value, true});
            continue;
        }
        const auto start = i;
        while (i < input.size()) {
            const char c2 = input[i];
            if (std::isspace(static_cast<unsigned char>(c2))) break;
            if (std::string_view("{}()=;,").find(c2) != std::string_view::npos) break;
            if (c2 == '/' && i + 1 < input.size() && (input[i + 1] == '*' || input[i + 1] == '/')) break;
            ++i;
        }
        if (i == start) {
            error = "unexpected character '" + std::string(1, c) + "'";
            return tokens;
        }
        tokens.push_back({TokenKind::String, input.substr(start, i - start), false});
    }
    tokens.push_back({TokenKind::End, {}, false});
    return tokens;
}

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    bool parse(PbxValue& root, std::string& error) {
        std::vector<std::string> header;
        collectComments(header);
        auto parsed = parseValue(error);
        if (!error.empty()) return false;
        parsed.leadingComments.insert(parsed.leadingComments.begin(), header.begin(), header.end());
        root = std::move(parsed);
        collectComments(root.trailingComments);
        if (peek().kind != TokenKind::End) {
            error = "unexpected trailing content near '" + peek().text + "'";
            return false;
        }
        return true;
    }

private:
    const Token& peek() const { return tokens_[index_]; }
    Token take() { return tokens_[index_++]; }
    void collectComments(std::vector<std::string>& target) {
        while (peek().kind == TokenKind::Comment) target.push_back(take().text);
    }

    bool expect(TokenKind kind, std::string& error, const char* description) {
        if (peek().kind != kind) {
            error = std::string("expected ") + description + ", found '" + peek().text + "'";
            return false;
        }
        ++index_;
        return true;
    }

    PbxValue parseValue(std::string& error) {
        PbxValue value;
        collectComments(value.leadingComments);
        if (peek().kind == TokenKind::LeftBrace) return parseDictionary(error);
        if (peek().kind == TokenKind::LeftParen) return parseArray(error);
        if (peek().kind == TokenKind::String) {
            auto token = take();
            value.kind = PbxValue::Kind::String;
            value.string = token.text;
            value.quoted = token.quoted;
            return value;
        }
        if (peek().kind == TokenKind::Data) {
            value = PbxValue::makeData(take().text);
            return value;
        }
        error = "expected a value, found '" + peek().text + "'";
        return value;
    }

    PbxValue parseDictionary(std::string& error) {
        auto value = PbxValue::makeDictionary();
        take(); // {
        while (true) {
            std::vector<std::string> leading;
            collectComments(leading);
            if (peek().kind == TokenKind::RightBrace) { take(); break; }
            if (peek().kind == TokenKind::End) { error = "unterminated dictionary"; return value; }
            if (peek().kind != TokenKind::String) {
                const std::string previous = index_ > 0 ? tokens_[index_ - 1].text : "<start>";
                error = "expected dictionary key, found '" + peek().text + "' after '" +
                        previous + "' (token " + std::to_string(index_) + ")";
                return value;
            }
            auto key = take();
            collectComments(leading); // e.g. "ID /* file name */ = { ... }"
            if (!expect(TokenKind::Equal, error, "'='")) return value;
            auto child = parseValue(error);
            if (!error.empty()) return value;
            std::vector<std::string> trailing;
            collectComments(trailing);
            if (peek().kind == TokenKind::Semicolon) {
                take();
                collectComments(trailing);
            }
            value.dictionary.push_back({key.text, key.quoted, std::move(child),
                                        std::move(leading), std::move(trailing)});
        }
        return value;
    }

    PbxValue parseArray(std::string& error) {
        auto value = PbxValue::makeArray();
        take(); // (
        while (true) {
            if (peek().kind == TokenKind::RightParen) { take(); break; }
            if (peek().kind == TokenKind::End) { error = "unterminated array"; return value; }
            auto child = parseValue(error);
            if (!error.empty()) return value;
            collectComments(child.trailingComments);
            if (peek().kind == TokenKind::Comma) {
                take();
                collectComments(child.trailingComments);
            } else if (peek().kind != TokenKind::RightParen) {
                error = "expected ',' or ')', found '" + peek().text + "'";
                return value;
            }
            value.array.push_back(std::move(child));
        }
        return value;
    }

    std::vector<Token> tokens_;
    std::size_t index_ = 0;
};

bool isBareSafe(std::string_view s) {
    if (s.empty()) return false;
    return std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '.' || c == '/' || c == '+' || c == '-' ||
               c == ':' || c == '$' || c == '(' || c == ')' || c == '@';
    });
}

std::string escapeString(std::string_view s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

void writeComments(const std::vector<std::string>& comments, int indent, std::string& out) {
    for (const auto& comment : comments) {
        if (comment.find('\n') != std::string::npos) {
            out.append(comment);
            if (!comment.empty() && comment.back() != '\n') out.push_back('\n');
        } else {
            out.append(std::string(static_cast<std::size_t>(indent), '\t'));
            out.append(comment);
            out.push_back('\n');
        }
    }
}

void writeTrailing(const std::vector<std::string>& comments, int indent, std::string& out) {
    for (const auto& comment : comments) {
        if (comment.find('\n') == std::string::npos && comment.size() < 90) {
            out.push_back(' ');
            out.append(comment);
        } else {
            out.push_back('\n');
            out.append(std::string(static_cast<std::size_t>(indent), '\t'));
            out.append(comment);
        }
    }
}

void writeValue(const PbxValue& value, int indent, std::string& out, bool inlineStart = false) {
    writeComments(value.leadingComments, indent, out);
    if (!inlineStart) out.append(std::string(static_cast<std::size_t>(indent), '\t'));
    switch (value.kind) {
    case PbxValue::Kind::String:
        if (value.quoted || !isBareSafe(value.string))
            out.push_back('"'), out.append(escapeString(value.string)), out.push_back('"');
        else
            out.append(value.string);
        break;
    case PbxValue::Kind::Data:
        out.push_back('<');
        out.append(value.data);
        out.push_back('>');
        break;
    case PbxValue::Kind::Dictionary: {
        out.append("{\n");
        for (const auto& entry : value.dictionary) {
            writeComments(entry.leadingComments, indent + 1, out);
            out.append(std::string(static_cast<std::size_t>(indent + 1), '\t'));
            if (entry.quoted || !isBareSafe(entry.key))
                out.push_back('"'), out.append(escapeString(entry.key)), out.push_back('"');
            else
                out.append(entry.key);
            out.append(" = ");
            writeValue(entry.value, indent + 1, out, true);
            writeTrailing(entry.trailingComments, indent + 1, out);
            out.append(";\n");
        }
        out.append(std::string(static_cast<std::size_t>(indent), '\t'));
        out.push_back('}');
        break;
    }
    case PbxValue::Kind::Array: {
        out.append("(\n");
        for (const auto& item : value.array) {
            writeValue(item, indent + 1, out);
            writeTrailing(item.trailingComments, indent + 1, out);
            out.append(",\n");
        }
        out.append(std::string(static_cast<std::size_t>(indent), '\t'));
        out.push_back(')');
        break;
    }
    }
    writeTrailing(value.trailingComments, indent, out);
}

void removeIdsFromArray(PbxValue& value, const std::string& id,
                        const std::vector<std::string>& arrayKeys) {
    if (value.kind == PbxValue::Kind::Dictionary) {
        for (auto& entry : value.dictionary) {
            if (std::find(arrayKeys.begin(), arrayKeys.end(), entry.key) != arrayKeys.end() &&
                entry.value.kind == PbxValue::Kind::Array) {
                auto& items = entry.value.array;
                items.erase(std::remove_if(items.begin(), items.end(), [&](const PbxValue& item) {
                                return item.kind == PbxValue::Kind::String && item.string == id;
                            }), items.end());
            }
            removeIdsFromArray(entry.value, id, arrayKeys);
        }
    } else if (value.kind == PbxValue::Kind::Array) {
        for (auto& item : value.array) removeIdsFromArray(item, id, arrayKeys);
    }
}

} // namespace

PbxValue PbxValue::makeString(std::string value, bool quote) {
    PbxValue result;
    result.kind = Kind::String;
    result.string = std::move(value);
    result.quoted = quote;
    return result;
}

PbxValue PbxValue::makeDictionary() {
    PbxValue result;
    result.kind = Kind::Dictionary;
    return result;
}

PbxValue PbxValue::makeArray() {
    PbxValue result;
    result.kind = Kind::Array;
    return result;
}

PbxValue PbxValue::makeData(std::string value) {
    PbxValue result;
    result.kind = Kind::Data;
    result.data = std::move(value);
    return result;
}

const PbxValue* PbxValue::find(const std::string& key) const {
    if (kind != Kind::Dictionary) return nullptr;
    for (const auto& entry : dictionary)
        if (entry.key == key) return &entry.value;
    return nullptr;
}

PbxValue* PbxValue::find(const std::string& key) {
    if (kind != Kind::Dictionary) return nullptr;
    for (auto& entry : dictionary)
        if (entry.key == key) return &entry.value;
    return nullptr;
}

std::string PbxValue::stringValue(const std::string& key, std::string fallback) const {
    const auto* value = find(key);
    return value && value->kind == Kind::String ? value->string : std::move(fallback);
}

std::vector<std::string> PbxValue::stringArray(const std::string& key) const {
    std::vector<std::string> result;
    const auto* value = find(key);
    if (value && value->kind == Kind::Array) {
        for (const auto& item : value->array)
            if (item.kind == Kind::String) result.push_back(item.string);
    }
    return result;
}

bool PbxValue::set(const std::string& key, PbxValue value) {
    if (kind != Kind::Dictionary) return false;
    for (auto& entry : dictionary) {
        if (entry.key == key) {
            entry.value = std::move(value);
            return true;
        }
    }
    dictionary.push_back({key, false, std::move(value), {}, {}});
    return true;
}

bool PbxValue::erase(const std::string& key) {
    if (kind != Kind::Dictionary) return false;
    const auto before = dictionary.size();
    dictionary.erase(std::remove_if(dictionary.begin(), dictionary.end(),
                                    [&](const PbxEntry& e) { return e.key == key; }),
                     dictionary.end());
    return dictionary.size() != before;
}

Expected<void> PbxDocument::parse(std::string text) {
    std::string error;
    auto tokens = lex(text, error);
    if (!error.empty()) return Expected<void>::failure(error);
    Parser parser(std::move(tokens));
    PbxValue root;
    if (!parser.parse(root, error)) return Expected<void>::failure(error);
    root_ = std::move(root);
    parsed_ = true;
    return {};
}

std::string PbxDocument::serialize() const {
    std::string out;
    writeValue(root_, 0, out);
    out.push_back('\n');
    return out;
}

PbxValue* PbxDocument::object(const std::string& identifier) {
    auto* objects = root_.find("objects");
    return objects ? objects->find(identifier) : nullptr;
}

const PbxValue* PbxDocument::object(const std::string& identifier) const {
    const auto* objects = root_.find("objects");
    return objects ? objects->find(identifier) : nullptr;
}

std::vector<std::string> PbxDocument::objectIds() const {
    std::vector<std::string> ids;
    const auto* objects = root_.find("objects");
    if (objects && objects->kind == PbxValue::Kind::Dictionary)
        for (const auto& entry : objects->dictionary) ids.push_back(entry.key);
    return ids;
}

std::vector<std::string> PbxDocument::objectIdsOfClass(const std::string& targetIsa) const {
    std::vector<std::string> result;
    for (const auto& id : objectIds())
        if (isa(id) == targetIsa) result.push_back(id);
    return result;
}

std::string PbxDocument::isa(const std::string& identifier) const {
    const auto* value = object(identifier);
    return value ? value->stringValue("isa") : std::string();
}

std::string PbxDocument::fieldString(const std::string& identifier, const std::string& key,
                                     std::string fallback) const {
    const auto* value = object(identifier);
    return value ? value->stringValue(key, std::move(fallback)) : std::move(fallback);
}

std::vector<std::string> PbxDocument::fieldArray(const std::string& identifier,
                                                 const std::string& key) const {
    const auto* value = object(identifier);
    return value ? value->stringArray(key) : std::vector<std::string>{};
}

bool PbxDocument::setField(const std::string& identifier, const std::string& key, PbxValue value) {
    auto* target = object(identifier);
    return target && target->set(key, std::move(value));
}

bool PbxDocument::setString(const std::string& identifier, const std::string& key,
                            std::string value, bool quote) {
    return setField(identifier, key, PbxValue::makeString(std::move(value), quote));
}

bool PbxDocument::appendToArray(const std::string& identifier, const std::string& key,
                                PbxValue value) {
    auto* target = object(identifier);
    if (!target) return false;
    auto* array = target->find(key);
    if (!array) {
        target->set(key, PbxValue::makeArray());
        array = target->find(key);
    }
    if (!array || array->kind != PbxValue::Kind::Array) return false;
    array->array.push_back(std::move(value));
    return true;
}

std::string PbxDocument::makeIdentifier(const std::string& hint) {
    auto existing = objectIds();
    std::sort(existing.begin(), existing.end());
    std::random_device device;
    std::mt19937 generator(device());
    std::uniform_int_distribution<int> distribution(0, 15);
    std::string id;
    do {
        std::ostringstream stream;
        stream << std::uppercase << std::hex << std::setfill('0');
        for (int i = 0; i < 24; ++i) stream << distribution(generator);
        id = stream.str();
    } while (std::binary_search(existing.begin(), existing.end(), id));
    (void)hint; // identifiers are plain 24-character hex strings; comments are stored separately
    return id;
}

std::string PbxDocument::addObject(std::string objectIsa, std::vector<PbxEntry> fields) {
    auto* objects = root_.find("objects");
    if (!objects) {
        root_.set("objects", PbxValue::makeDictionary());
        objects = root_.find("objects");
    }
    auto id = makeIdentifier();
    PbxValue value = PbxValue::makeDictionary();
    value.set("isa", PbxValue::makeString(std::move(objectIsa)));
    for (auto& field : fields) value.dictionary.push_back(std::move(field));
    objects->dictionary.push_back({id, true, std::move(value), {}, {}});
    return id;
}

bool PbxDocument::removeObject(const std::string& identifier) {
    auto* objects = root_.find("objects");
    if (!objects || objects->kind != PbxValue::Kind::Dictionary) return false;
    const auto before = objects->dictionary.size();
    objects->dictionary.erase(
        std::remove_if(objects->dictionary.begin(), objects->dictionary.end(),
                       [&](const PbxEntry& e) { return e.key == identifier; }),
        objects->dictionary.end());
    if (objects->dictionary.size() == before) return false;
    pruneReferences(identifier);
    return true;
}

int PbxDocument::pruneReferences(const std::string& identifier) {
    const std::vector<std::string> keys = {"children", "files", "buildPhases", "dependencies",
                                           "buildRules", "packageReferences", "subprojects"};
    int removed = 0;
    auto* objects = root_.find("objects");
    if (!objects) return 0;
    for (auto& entry : objects->dictionary) {
        for (const auto& key : keys) {
            auto* array = entry.value.find(key);
            if (!array || array->kind != PbxValue::Kind::Array) continue;
            const auto before = array->array.size();
            array->array.erase(
                std::remove_if(array->array.begin(), array->array.end(), [&](const PbxValue& item) {
                    return item.kind == PbxValue::Kind::String && item.string == identifier;
                }), array->array.end());
            removed += static_cast<int>(before - array->array.size());
        }
    }
    removeIdsFromArray(root_, identifier, keys);
    return removed;
}

std::vector<std::string> PbxDocument::validate() const {
    std::vector<std::string> issues;
    if (!parsed_) { issues.push_back("project.pbxproj has not been parsed"); return issues; }
    const auto* objects = root_.find("objects");
    if (!objects || objects->kind != PbxValue::Kind::Dictionary) {
        issues.push_back("missing objects dictionary");
        return issues;
    }
    if (rootObjectId().empty()) issues.push_back("missing rootObject");
    else if (!object(rootObjectId())) issues.push_back("rootObject points to a missing object");

    for (const auto& id : objectIds()) {
        const auto* value = object(id);
        if (!value) continue;
        if (value->stringValue("isa").empty())
            issues.push_back("object " + id + " has no isa");
        if (isa(id) == "PBXBuildFile" && !object(value->stringValue("fileRef")))
            issues.push_back("build file " + id + " has a broken fileRef");
        for (const auto& child : fieldArray(id, "children"))
            if (!object(child)) issues.push_back("group " + id + " has a missing child " + child);
        for (const auto& phase : fieldArray(id, "buildPhases"))
            if (!object(phase)) issues.push_back("target " + id + " has a missing build phase " + phase);
    }
    return issues;
}

std::string PbxDocument::rootObjectId() const {
    return root_.stringValue("rootObject");
}

} // namespace lxcode::pbx
