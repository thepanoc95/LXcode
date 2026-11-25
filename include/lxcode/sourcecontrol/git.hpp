#pragma once
#include "lxcode/core/expected.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace lxcode {

struct GitStatusEntry {
    std::string path;
    char indexStatus = ' ';
    char worktreeStatus = ' ';

    bool staged() const { return indexStatus != ' ' && indexStatus != '?'; }
    bool modified() const { return worktreeStatus == 'M' || indexStatus == 'M'; }
    bool untracked() const { return indexStatus == '?' && worktreeStatus == '?'; }
    bool conflicted() const { return indexStatus == 'U' || worktreeStatus == 'U'; }
    std::string label() const;
};

struct GitBranch {
    std::string name;
    bool current = false;
};

struct GitCommit {
    std::string hash;
    std::string author;
    std::string date;
    std::string subject;
};

struct GitDiff {
    std::string path;
    std::string text;
};

class GitRepository {
public:
    static Expected<GitRepository> open(const std::filesystem::path& directory);
    static Expected<GitRepository> init(const std::filesystem::path& directory);

    std::filesystem::path root() const { return root_; }

    Expected<std::vector<GitStatusEntry>> status() const;
    Expected<void> stage(const std::vector<std::string>& paths);
    Expected<void> unstage(const std::vector<std::string>& paths);
    Expected<void> commit(const std::string& message);
    Expected<std::vector<GitBranch>> branches() const;
    Expected<std::string> currentBranch() const;
    Expected<void> checkout(const std::string& branch);
    Expected<void> createBranch(const std::string& name, bool checkout = true);
    Expected<std::vector<GitCommit>> log(int count = 40) const;
    Expected<GitDiff> diff(const std::string& path, bool staged = false) const;
    Expected<std::vector<std::string>> remotes() const;
    Expected<void> push(const std::string& remote, const std::string& branch);
    Expected<void> pull(const std::string& remote, const std::string& branch);
    Expected<std::vector<std::string>> conflicts() const;

private:
    std::filesystem::path root_;

    explicit GitRepository(std::filesystem::path root) : root_(std::move(root)) {}
    Expected<std::string> run(const std::vector<std::string>& arguments,
                              std::string* errorOutput = nullptr) const;
};

bool isGitAvailable();

} // namespace lxcode
