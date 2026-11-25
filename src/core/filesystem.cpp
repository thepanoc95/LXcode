#include "lxcode/core/filesystem.hpp"

#include <fstream>
#include <sstream>

namespace lxcode::fs {

Expected<std::string> readText(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return Expected<std::string>::failure("cannot open " + path.string());
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (!stream.good() && !stream.eof())
        return Expected<std::string>::failure("cannot read " + path.string());
    return buffer.str();
}

Expected<void> writeTextAtomic(const std::filesystem::path& path, const std::string& contents,
                               bool keepBackup) {
    std::error_code ec;
    const auto parent = path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path();
    std::filesystem::create_directories(parent, ec);
    if (ec) return Expected<void>::failure("cannot create directory: " + ec.message());

    const auto temporary = parent / ("." + path.filename().string() + ".lxcode-tmp");
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) return Expected<void>::failure("cannot create " + temporary.string());
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        stream.flush();
        if (!stream) return Expected<void>::failure("cannot write " + temporary.string());
    }

    if (keepBackup && std::filesystem::exists(path, ec)) {
        const auto backup = parent / (path.filename().string() + ".lxcode-backup");
        std::filesystem::remove(backup, ec);
        std::filesystem::rename(path, backup, ec);
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) return Expected<void>::failure("cannot replace " + path.string() + ": " + ec.message());
    return {};
}

bool exists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

std::string extension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
    return ext;
}

std::vector<std::filesystem::path> filesWithExtension(const std::filesystem::path& root,
                                                       const std::string& ext, int maxDepth) {
    std::vector<std::filesystem::path> result;
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) return result;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (maxDepth >= 0 && it.depth() > maxDepth) {
            it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        if (extension(it->path()) == ext) result.push_back(it->path());
    }
    return result;
}

} // namespace lxcode::fs
