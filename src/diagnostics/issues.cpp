#include "lxcode/diagnostics/issues.hpp"

#include <algorithm>

namespace lxcode {

void DiagnosticCenter::add(Issue issue) { issues_.push_back(std::move(issue)); }

void DiagnosticCenter::clear(const std::string& category) {
    if (category.empty()) {
        issues_.clear();
        return;
    }
    issues_.erase(std::remove_if(issues_.begin(), issues_.end(),
                                 [&](const Issue& issue) { return issue.category == category; }),
                  issues_.end());
}

int DiagnosticCenter::errorCount() const {
    return static_cast<int>(std::count_if(issues_.begin(), issues_.end(), [](const Issue& issue) {
        return issue.severity == Issue::Severity::Error;
    }));
}

int DiagnosticCenter::warningCount() const {
    return static_cast<int>(std::count_if(issues_.begin(), issues_.end(), [](const Issue& issue) {
        return issue.severity == Issue::Severity::Warning;
    }));
}

std::vector<Issue> inspectProject(const XcodeProject& project) {
    std::vector<Issue> result;
    for (const auto& message : project.diagnostics()) {
        Issue issue;
        issue.severity = Issue::Severity::Error;
        issue.category = "Project";
        issue.message = message;
        result.push_back(std::move(issue));
    }
    for (const auto& target : project.targets()) {
        const auto settings = project.buildSettings(target.id, "Debug");
        const auto sdk = settings.count("SDKROOT") ? settings.at("SDKROOT") : std::string();
        if (sdk.empty()) {
            Issue issue;
            issue.severity = Issue::Severity::Warning;
            issue.category = "Configuration";
            issue.message = "Target " + target.name + " has no SDKROOT setting";
            result.push_back(std::move(issue));
        }
        if (target.productType.find("xctest") != std::string::npos) {
            Issue issue;
            issue.severity = Issue::Severity::Info;
            issue.category = "Unsupported";
            issue.message = "XCTest target " + target.name +
                            " metadata is preserved, but execution requires an Apple runtime";
            result.push_back(std::move(issue));
        }
        if (target.productType.find("application") != std::string::npos) {
            Issue issue;
            issue.severity = Issue::Severity::Info;
            issue.category = "Unsupported";
            issue.message = "Application target " + target.name +
                            " may require Apple frameworks that are unavailable on Linux";
            result.push_back(std::move(issue));
        }
    }
    return result;
}

std::vector<Issue> inspectToolchain(const Toolchain& toolchain) {
    std::vector<Issue> result;
    for (const auto& error : toolchain.errors) {
        Issue issue;
        issue.severity = Issue::Severity::Error;
        issue.category = "Toolchain";
        issue.message = error;
        issue.detail = toolchain.root.string();
        result.push_back(std::move(issue));
    }
    for (const auto& warning : toolchain.warnings) {
        Issue issue;
        issue.severity = Issue::Severity::Warning;
        issue.category = "Toolchain";
        issue.message = warning;
        issue.detail = toolchain.name;
        result.push_back(std::move(issue));
    }
    if (toolchain.kind == ToolchainKind::OSXCross && toolchain.sdk.empty()) {
        Issue issue;
        issue.severity = Issue::Severity::Error;
        issue.category = "SDK";
        issue.message = "No macOS SDK is configured for " + toolchain.name;
        issue.detail = "Set OSXCROSS_ROOT or configure a toolchain with an SDK under SDK/";
        result.push_back(std::move(issue));
    }
    return result;
}

} // namespace lxcode
