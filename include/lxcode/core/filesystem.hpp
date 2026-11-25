#pragma once
#include "lxcode/core/expected.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace lxcode::fs {

Expected<std::string> readText(const std::filesystem::path& path);
Expected<void> writeTextAtomic(const std::filesystem::path& path, const std::string& contents,
                               bool keepBackup = true);
bool exists(const std::filesystem::path& path);
std::string extension(const std::filesystem::path& path);
std::vector<std::filesystem::path> filesWithExtension(const std::filesystem::path& root,
                                                       const std::string& extension,
                                                       int maxDepth = -1);

} // namespace lxcode::fs
