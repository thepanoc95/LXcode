#pragma once
#include "lxcode/core/expected.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace lxcode {

class Settings {
public:
    static std::filesystem::path defaultPath();

    Expected<void> load(const std::filesystem::path& path = defaultPath());
    Expected<void> save(const std::filesystem::path& path = defaultPath()) const;

    nlohmann::json& data() { return data_; }
    const nlohmann::json& data() const { return data_; }

    std::string string(const std::string& key, std::string fallback = {}) const;
    bool boolean(const std::string& key, bool fallback = false) const;
    int integer(const std::string& key, int fallback = 0) const;
    std::vector<std::string> stringList(const std::string& key) const;
    void set(const std::string& key, nlohmann::json value);

    std::vector<std::filesystem::path> recentProjects() const;
    void addRecentProject(const std::filesystem::path& path);

    std::string languageServerCommand(const std::string& language) const;
    void setLanguageServerCommand(const std::string& language, const std::string& command);

private:
    nlohmann::json data_ = nlohmann::json::object();
};

} // namespace lxcode
