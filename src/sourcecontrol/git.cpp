#include "lxcode/sourcecontrol/git.hpp"

#include "lxcode/core/process.hpp"
#include "lxcode/core/strings.hpp"

#include <algorithm>
#include <sstream>

namespace lxcode {

namespace {
constexpr char kFieldSeparator = '\x1f';
}

std::string GitStatusEntry::label() const {
    if (conflicted()) return "Conflict";
    if (untracked()) return "Untracked";
    if (staged() && worktreeStatus == 'M') return "Staged + Modified";
    if (staged()) return indexStatus == 'A' ? "Added" : "Staged";
    if (worktreeStatus == 'M') return "Modified";
    if (worktreeStatus == 'D') return "Deleted";
    return "Changed";
}

bool isGitAvailable() { return !which("git").empty(); }

Expected<GitRepository> GitRepository::open(const std::filesystem::path& directory) {
    if (!isGitAvailable()) return Expected<GitRepository>::failure("git is not installed");
    ProcessOptions options;
    options.workingDirectory = directory.string();
    options.timeout = std::chrono::seconds(5);
    auto result = runProcess("git", {"rev-parse", "--show-toplevel"}, options);
    if (result.exitCode != 0)
        return Expected<GitRepository>::failure("not a git repository: " + directory.string());
    return GitRepository(strings::trim(result.stdoutText));
}

Expected<GitRepository> GitRepository::init(const std::filesystem::path& directory) {
    if (!isGitAvailable()) return Expected<GitRepository>::failure("git is not installed");
    ProcessOptions options;
    options.workingDirectory = directory.string();
    auto result = runProcess("git", {"init"}, options);
    if (result.exitCode != 0)
        return Expected<GitRepository>::failure(result.stderrText.empty() ? "git init failed"
                                                                          : strings::trim(result.stderrText));
    return GitRepository(directory);
}

Expected<std::string> GitRepository::run(const std::vector<std::string>& arguments,
                                         std::string* errorOutput) const {
    ProcessOptions options;
    options.workingDirectory = root_.string();
    options.timeout = std::chrono::seconds(60);
    options.mergeStreams = false;
    auto result = runProcess("git", arguments, options);
    if (result.exitCode != 0) {
        const auto message = strings::trim(result.stderrText.empty() ? result.stdoutText
                                                                     : result.stderrText);
        if (errorOutput) *errorOutput = message;
        return Expected<std::string>::failure(message.empty() ? "git command failed" : message);
    }
    return result.stdoutText;
}

Expected<std::vector<GitStatusEntry>> GitRepository::status() const {
    auto output = run({"status", "--porcelain=v1", "--untracked-files=all"});
    if (!output) return Expected<std::vector<GitStatusEntry>>::failure(output.error());
    std::vector<GitStatusEntry> result;
    std::istringstream stream(*output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.size() < 4) continue;
        GitStatusEntry entry;
        entry.indexStatus = line[0];
        entry.worktreeStatus = line[1];
        auto path = line.substr(3);
        const auto arrow = path.find(" -> ");
        if (arrow != std::string::npos) path = path.substr(arrow + 4);
        if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
            path = path.substr(1, path.size() - 2);
        entry.path = path;
        result.push_back(std::move(entry));
    }
    return result;
}

Expected<void> GitRepository::stage(const std::vector<std::string>& paths) {
    std::vector<std::string> args = {"add", "--"};
    args.insert(args.end(), paths.begin(), paths.end());
    auto result = run(args);
    return result ? Expected<void>() : Expected<void>::failure(result.error());
}

Expected<void> GitRepository::unstage(const std::vector<std::string>& paths) {
    std::vector<std::string> args = {"restore", "--staged", "--"};
    args.insert(args.end(), paths.begin(), paths.end());
    auto result = run(args);
    return result ? Expected<void>() : Expected<void>::failure(result.error());
}

Expected<void> GitRepository::commit(const std::string& message) {
    auto result = run({"commit", "-m", message});
    return result ? Expected<void>() : Expected<void>::failure(result.error());
}

Expected<std::vector<GitBranch>> GitRepository::branches() const {
    auto output = run({"branch", "--format=%(refname:short)|%(HEAD)"});
    if (!output) return Expected<std::vector<GitBranch>>::failure(output.error());
    std::vector<GitBranch> result;
    std::istringstream stream(*output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        const auto pipe = line.rfind('|');
        GitBranch branch;
        branch.name = line.substr(0, pipe);
        branch.current = pipe != std::string::npos && line.substr(pipe + 1).find('*') != std::string::npos;
        result.push_back(std::move(branch));
    }
    return result;
}

Expected<std::string> GitRepository::currentBranch() const {
    auto output = run({"rev-parse", "--abbrev-ref", "HEAD"});
    if (!output) return Expected<std::string>::failure(output.error());
    return strings::trim(*output);
}

Expected<void> GitRepository::checkout(const std::string& branch) {
    auto result = run({"checkout", branch});
    return result ? Expected<void>() : Expected<void>::failure(result.error());
}

Expected<void> GitRepository::createBranch(const std::string& name, bool checkoutBranch) {
    auto result = checkoutBranch ? run({"checkout", "-b", name}) : run({"branch", name});
    return result ? Expected<void>() : Expected<void>::failure(result.error());
}

Expected<std::vector<GitCommit>> GitRepository::log(int count) const {
    auto output = run({"log", "-" + std::to_string(count),
                       "--pretty=format:%H%x1f%an%x1f%ad%x1f%s", "--date=short"});
    if (!output) return Expected<std::vector<GitCommit>>::failure(output.error());
    std::vector<GitCommit> result;
    std::istringstream stream(*output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        const auto fields = strings::split(line, kFieldSeparator);
        if (fields.size() < 4) continue;
        GitCommit commit;
        commit.hash = fields[0];
        commit.author = fields[1];
        commit.date = fields[2];
        commit.subject = fields[3];
        result.push_back(std::move(commit));
    }
    return result;
}

Expected<GitDiff> GitRepository::diff(const std::string& path, bool staged) const {
    std::vector<std::string> args = {"diff", "--no-color"};
    if (staged) args.push_back("--cached");
    if (!path.empty()) {
        args.push_back("--");
        args.push_back(path);
    }
    auto output = run(args);
    if (!output) return Expected<GitDiff>::failure(output.error());
    GitDiff result;
    result.path = path;
    result.text = *output;
    return result;
}

Expected<std::vector<std::string>> GitRepository::remotes() const {
    auto output = run({"remote"});
    if (!output) return Expected<std::vector<std::string>>::failure(output.error());
    const auto names = strings::trim(*output);
    if (names.empty()) return std::vector<std::string>{};
    return strings::split(names, '\n');
}

Expected<void> GitRepository::push(const std::string& remote, const std::string& branch) {
    auto result = run({"push", remote, branch});
    return result ? Expected<void>() : Expected<void>::failure(result.error());
}

Expected<void> GitRepository::pull(const std::string& remote, const std::string& branch) {
    auto result = run({"pull", remote, branch});
    return result ? Expected<void>() : Expected<void>::failure(result.error());
}

Expected<std::vector<std::string>> GitRepository::conflicts() const {
    auto status = this->status();
    if (!status) return Expected<std::vector<std::string>>::failure(status.error());
    std::vector<std::string> result;
    for (const auto& entry : *status)
        if (entry.conflicted()) result.push_back(entry.path);
    return result;
}

} // namespace lxcode
