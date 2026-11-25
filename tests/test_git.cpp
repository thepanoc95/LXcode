#include "test_support.hpp"

#include "lxcode/core/process.hpp"
#include "lxcode/sourcecontrol/git.hpp"

using namespace lxcode;
using namespace lxcode::test;

int main() {
    if (!isGitAvailable()) {
        std::cout << "git tests skipped (git unavailable)\n";
        return 0;
    }

    const auto root = tempDirectory("git");
    ProcessOptions options;
    options.workingDirectory = root.string();
    auto init = GitRepository::init(root);
    LXCHECK_TRUE(init);
    LXCHECK_TRUE(runProcess("git", {"config", "user.email", "test@example.com"}, options).exitCode == 0);
    LXCHECK_TRUE(runProcess("git", {"config", "user.name", "LXcode Tests"}, options).exitCode == 0);

    writeFile(root / "hello.txt", "hello\n");
    auto status = init->status();
    LXCHECK_TRUE(status);
    LXCHECK_EQ(status->size(), std::size_t(1));
    LXCHECK_TRUE(status->front().untracked());

    LXCHECK_TRUE(init->stage({"hello.txt"}));
    status = init->status();
    LXCHECK_TRUE(status);
    LXCHECK_TRUE(status->front().staged());

    LXCHECK_TRUE(init->commit("Initial commit"));
    status = init->status();
    LXCHECK_TRUE(status);
    LXCHECK_TRUE(status->empty());

    auto log = init->log(5);
    LXCHECK_TRUE(log);
    LXCHECK_EQ(log->size(), std::size_t(1));
    LXCHECK_EQ(log->front().subject, std::string("Initial commit"));

    writeFile(root / "hello.txt", "changed\n");
    auto diff = init->diff("hello.txt");
    LXCHECK_TRUE(diff);
    LXCHECK_TRUE(diff->text.find("changed") != std::string::npos);

    LXCHECK_TRUE(init->createBranch("feature", true));
    auto branch = init->currentBranch();
    LXCHECK_TRUE(branch);
    LXCHECK_EQ(*branch, std::string("feature"));
    auto branches = init->branches();
    LXCHECK_TRUE(branches);
    LXCHECK_TRUE(branches->size() >= 2);

    auto remotes = init->remotes();
    LXCHECK_TRUE(remotes);
    LXCHECK_TRUE(remotes->empty());

    std::cout << "git tests passed\n";
    return 0;
}
