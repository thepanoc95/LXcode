#pragma once
#include "lxcode/core/expected.hpp"
#include "lxcode/project/xcodeproject.hpp"
#include "lxcode/toolchains/toolchain.hpp"

#include <string>
#include <vector>

namespace lxcode {

struct Issue {
    enum class Severity { Info, Warning, Error };
    Severity severity = Severity::Info;
    std::string category;
    std::string message;
    std::string detail;
    std::string file;
};

class DiagnosticCenter {
public:
    void add(Issue issue);
    void clear(const std::string& category = {});
    std::vector<Issue> issues() const { return issues_; }
    int errorCount() const;
    int warningCount() const;

private:
    std::vector<Issue> issues_;
};

std::vector<Issue> inspectProject(const XcodeProject& project);
std::vector<Issue> inspectToolchain(const Toolchain& toolchain);

} // namespace lxcode
