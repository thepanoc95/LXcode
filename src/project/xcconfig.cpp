#include "lxcode/project/xcconfig.hpp"

#include "lxcode/core/strings.hpp"

#include <algorithm>
#include <sstream>

namespace lxcode {

Expected<void> XcconfigDocument::parse(std::string text) {
    lines_.clear();
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        XcconfigLine parsed;
        parsed.raw = line;
        const auto trimmed = strings::trim(line);
        if (trimmed.empty()) {
            parsed.kind = XcconfigLine::Kind::Blank;
        } else if (strings::startsWith(trimmed, "#include")) {
            parsed.kind = XcconfigLine::Kind::Include;
            parsed.value = strings::trim(trimmed.substr(std::string("#include").size()));
        } else if (strings::startsWith(trimmed, "//") || strings::startsWith(trimmed, "#")) {
            parsed.kind = XcconfigLine::Kind::Comment;
        } else {
            const auto equals = trimmed.find('=');
            if (equals == std::string::npos) {
                parsed.kind = XcconfigLine::Kind::Comment;
            } else {
                parsed.kind = XcconfigLine::Kind::Setting;
                parsed.key = strings::trim(trimmed.substr(0, equals));
                parsed.value = strings::trim(trimmed.substr(equals + 1));
            }
        }
        lines_.push_back(std::move(parsed));
    }
    return {};
}

std::string XcconfigDocument::serialize() const {
    std::string out;
    for (const auto& line : lines_) {
        out.append(line.raw);
        out.push_back('\n');
    }
    return out;
}

std::vector<std::string> XcconfigDocument::settingNames() const {
    std::vector<std::string> result;
    for (const auto& line : lines_)
        if (line.kind == XcconfigLine::Kind::Setting) result.push_back(line.key);
    return result;
}

std::string XcconfigDocument::setting(const std::string& key, int depth) const {
    if (depth > 12) return {};
    for (const auto& line : lines_) {
        if (line.kind != XcconfigLine::Kind::Setting || line.key != key) continue;
        std::string value = line.value;
        std::size_t cursor = 0;
        while (true) {
            const auto start = value.find("$(", cursor);
            if (start == std::string::npos) break;
            const auto end = value.find(')', start + 2);
            if (end == std::string::npos) break;
            const auto variable = value.substr(start + 2, end - start - 2);
            const auto resolved = setting(variable, depth + 1);
            value.replace(start, end + 1 - start, resolved);
            cursor = start + resolved.size();
        }
        return value;
    }
    return {};
}

std::vector<std::string> XcconfigDocument::includes() const {
    std::vector<std::string> result;
    for (const auto& line : lines_) {
        if (line.kind != XcconfigLine::Kind::Include) continue;
        auto value = line.value;
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);
        result.push_back(value);
    }
    return result;
}

void XcconfigDocument::setSetting(const std::string& key, const std::string& value) {
    for (auto& line : lines_) {
        if (line.kind == XcconfigLine::Kind::Setting && line.key == key) {
            line.value = value;
            line.raw = key + " = " + value;
            return;
        }
    }
    XcconfigLine line;
    line.kind = XcconfigLine::Kind::Setting;
    line.key = key;
    line.value = value;
    line.raw = key + " = " + value;
    lines_.push_back(std::move(line));
}

bool XcconfigDocument::removeSetting(const std::string& key) {
    const auto before = lines_.size();
    lines_.erase(std::remove_if(lines_.begin(), lines_.end(), [&](const XcconfigLine& line) {
                     return line.kind == XcconfigLine::Kind::Setting && line.key == key;
                 }), lines_.end());
    return lines_.size() != before;
}

} // namespace lxcode
