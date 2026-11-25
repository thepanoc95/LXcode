#include "lxcode/config/settings.hpp"

#include "lxcode/core/filesystem.hpp"

#include <algorithm>
#include <cstdlib>

namespace lxcode {

std::filesystem::path Settings::defaultPath() {
    const char* configHome = std::getenv("XDG_CONFIG_HOME");
    std::filesystem::path base = configHome && *configHome ? configHome : std::string();
    if (base.empty()) {
        const char* home = std::getenv("HOME");
        base = home ? std::filesystem::path(home) / ".config" : std::filesystem::path(".config");
    }
    return base / "LXcode" / "settings.json";
}

Expected<void> Settings::load(const std::filesystem::path& path) {
    if (!fs::exists(path)) {
        data_ = nlohmann::json::object();
        return {};
    }
    auto text = fs::readText(path);
    if (!text) return Expected<void>::failure(text.error());
    try {
        data_ = nlohmann::json::parse(*text);
    } catch (const std::exception& error) {
        return Expected<void>::failure(std::string("invalid settings file: ") + error.what());
    }
    return {};
}

Expected<void> Settings::save(const std::filesystem::path& path) const {
    return fs::writeTextAtomic(path, data_.dump(2) + "\n");
}

std::string Settings::string(const std::string& key, std::string fallback) const {
    if (!data_.contains(key) || !data_[key].is_string()) return fallback;
    return data_[key].get<std::string>();
}

bool Settings::boolean(const std::string& key, bool fallback) const {
    if (!data_.contains(key) || !data_[key].is_boolean()) return fallback;
    return data_[key].get<bool>();
}

int Settings::integer(const std::string& key, int fallback) const {
    if (!data_.contains(key) || !data_[key].is_number_integer()) return fallback;
    return data_[key].get<int>();
}

std::vector<std::string> Settings::stringList(const std::string& key) const {
    if (!data_.contains(key) || !data_[key].is_array()) return {};
    return data_[key].get<std::vector<std::string>>();
}

void Settings::set(const std::string& key, nlohmann::json value) { data_[key] = std::move(value); }

std::vector<std::filesystem::path> Settings::recentProjects() const {
    std::vector<std::filesystem::path> result;
    for (const auto& item : stringList("recentProjects")) result.emplace_back(item);
    return result;
}

void Settings::addRecentProject(const std::filesystem::path& path) {
    auto recent = stringList("recentProjects");
    const auto value = path.string();
    recent.erase(std::remove(recent.begin(), recent.end(), value), recent.end());
    recent.insert(recent.begin(), value);
    if (recent.size() > 12) recent.resize(12);
    set("recentProjects", recent);
}

std::string Settings::languageServerCommand(const std::string& language) const {
    if (!data_.contains("languageServers") || !data_["languageServers"].is_object()) return {};
    const auto& servers = data_["languageServers"];
    if (!servers.contains(language) || !servers[language].is_string()) return {};
    return servers[language].get<std::string>();
}

void Settings::setLanguageServerCommand(const std::string& language, const std::string& command) {
    if (!data_.contains("languageServers") || !data_["languageServers"].is_object())
        data_["languageServers"] = nlohmann::json::object();
    data_["languageServers"][language] = command;
}

} // namespace lxcode
