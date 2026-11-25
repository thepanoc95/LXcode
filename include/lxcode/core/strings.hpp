#pragma once
#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace lxcode::strings {

inline std::string trim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(first, last - first + 1));
}

inline std::string ltrim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    return first == std::string_view::npos ? std::string() : std::string(s.substr(first));
}

inline std::string rtrim(std::string_view s) {
    const auto last = s.find_last_not_of(" \t\r\n");
    return last == std::string_view::npos ? std::string() : std::string(s.substr(0, last + 1));
}

inline std::string toLower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

inline bool startsWith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

inline bool endsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

inline std::string replaceAll(std::string subject, std::string_view from, std::string_view to) {
    if (from.empty()) return subject;
    std::size_t pos = 0;
    while ((pos = subject.find(from, pos)) != std::string::npos) {
        subject.replace(pos, from.size(), to);
        pos += to.size();
    }
    return subject;
}

inline std::vector<std::string> split(std::string_view s, char delimiter) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (true) {
        const auto pos = s.find(delimiter, start);
        if (pos == std::string_view::npos) {
            result.emplace_back(s.substr(start));
            break;
        }
        result.emplace_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return result;
}

inline std::string join(const std::vector<std::string>& items, std::string_view separator) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) out.append(separator);
        out.append(items[i]);
    }
    return out;
}

inline std::string quoteShell(std::string_view s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\' || c == '$' || c == '`') out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

inline std::string normalize(std::string_view s) {
    std::string out(s);
    std::replace(out.begin(), out.end(), '\\', '/');
    return out;
}

inline std::string relativePath(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    auto rel = std::filesystem::relative(to, from, ec);
    if (ec) return to.string();
    return strings::normalize(rel.generic_string());
}

} // namespace lxcode::strings
