#pragma once
#include "lxcode/core/expected.hpp"

#include <string>
#include <vector>

namespace lxcode {

struct XcconfigLine {
    enum class Kind { Blank, Comment, Include, Setting };
    Kind kind = Kind::Blank;
    std::string raw;
    std::string key;
    std::string value;
};

class XcconfigDocument {
public:
    Expected<void> parse(std::string text);
    std::string serialize() const;

    const std::vector<XcconfigLine>& lines() const { return lines_; }
    std::vector<std::string> settingNames() const;
    std::string setting(const std::string& key, int depth = 0) const;
    std::vector<std::string> includes() const;
    void setSetting(const std::string& key, const std::string& value);
    bool removeSetting(const std::string& key);

private:
    std::vector<XcconfigLine> lines_;
};

} // namespace lxcode
