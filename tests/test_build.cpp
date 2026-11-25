#include "test_support.hpp"

#include "lxcode/build/buildsystem.hpp"

#include <algorithm>

using namespace lxcode;
using namespace lxcode::test;

int main() {
    auto diagnostics = parseDiagnostics(R"(main.cpp:10:5: error: unknown type
main.cpp:20:1: warning: unused variable
error: link failed
)");
    LXCHECK_EQ(diagnostics.size(), std::size_t(3));
    int errors = 0;
    int warnings = 0;
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == Diagnostic::Severity::Error) ++errors;
        if (diagnostic.severity == Diagnostic::Severity::Warning) ++warnings;
    }
    LXCHECK_EQ(errors, 2);
    LXCHECK_EQ(warnings, 1);
    auto sourceDiagnostic = std::find_if(
        diagnostics.begin(), diagnostics.end(), [](const Diagnostic& item) {
            return item.file == "main.cpp";
        });
    LXCHECK_TRUE(sourceDiagnostic != diagnostics.end());
    LXCHECK_EQ(sourceDiagnostic->line, 10);
    LXCHECK_EQ(sourceDiagnostic->severityName(), std::string("error"));

    BuildGraph graph;
    BuildNode first;
    first.id = "first";
    first.name = "First";
    BuildCommand command;
    command.kind = CommandKind::Generic;
    command.description = "Create output";
    command.executable = "/bin/sh";
    command.arguments = {"-c", "echo built"};
    first.commands.push_back(command);
    graph.add(first);

    BuildNode second;
    second.id = "second";
    second.dependencies = {"first"};
    second.commands.push_back(command);
    graph.add(second);
    LXCHECK_TRUE(graph.validate().empty());
    auto order = graph.topologicalOrder();
    LXCHECK_TRUE(order);
    LXCHECK_EQ(order->size(), std::size_t(2));
    LXCHECK_EQ(order->front(), std::string("first"));

    BuildGraph cycle;
    BuildNode a;
    a.id = "a";
    a.dependencies = {"b"};
    BuildNode b;
    b.id = "b";
    b.dependencies = {"a"};
    cycle.add(a);
    cycle.add(b);
    auto cycleOrder = cycle.topologicalOrder();
    LXCHECK_TRUE(!cycleOrder);
    LXCHECK_TRUE(cycleOrder.error().find("cycle") != std::string::npos);

    const auto root = tempDirectory("build");
    const auto output = root / "output.txt";
    BuildGraph execution;
    BuildNode task;
    task.id = "task";
    BuildCommand create;
    create.description = "Write output";
    create.executable = "/bin/sh";
    create.arguments = {"-c", std::string("printf 'ok' > ") + output.string()};
    create.output = output;
    task.commands.push_back(create);
    execution.add(task);

    BuildRequest request;
    request.configuration = "Debug";
    request.architecture = "arm64";
    request.buildDirectory = root;
    BuildController controller(request);
    controller.graph() = execution;
    controller.start(nullptr, nullptr);
    auto result = controller.wait();
    LXCHECK_TRUE(result.success);
    LXCHECK_EQ(result.commandCount, 1);
    LXCHECK_EQ(readFile(output), std::string("ok"));

    Toolchain toolchain;
    toolchain.kind = ToolchainKind::OSXCross;
    toolchain.name = "Fake OSXCross";
    toolchain.sdk = root / "SDK" / "MacOSX14.0.sdk";
    toolchain.tools["clang"] = root / "bin" / "clang";
    toolchain.tools["clang++"] = root / "bin" / "clang++";
    const auto toolchainFile = root / "toolchain.cmake";
    auto generated = generateOsxcrossToolchainFile(toolchain, "arm64", toolchainFile);
    LXCHECK_TRUE(generated);
    const auto text = readFile(toolchainFile);
    LXCHECK_TRUE(text.find("CMAKE_SYSTEM_NAME Darwin") != std::string::npos);
    LXCHECK_TRUE(text.find("CMAKE_OSX_ARCHITECTURES arm64") != std::string::npos);
    LXCHECK_TRUE(text.find("clang++") != std::string::npos);

    std::cout << "build tests passed\n";
    return 0;
}
