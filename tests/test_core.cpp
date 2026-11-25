#include "test_support.hpp"

#include "lxcode/core/expected.hpp"
#include "lxcode/core/filesystem.hpp"
#include "lxcode/core/process.hpp"
#include "lxcode/core/strings.hpp"

#include <algorithm>

using namespace lxcode;
using namespace lxcode::test;

int main() {
    Expected<int> value(42);
    LXCHECK_TRUE(value);
    LXCHECK_EQ(*value, 42);
    auto missing = Expected<int>::failure("nope");
    LXCHECK_TRUE(!missing);
    LXCHECK_EQ(missing.error(), std::string("nope"));

    LXCHECK_EQ(strings::trim("  hello \n"), std::string("hello"));
    LXCHECK_EQ(strings::toLower("HeLLo"), std::string("hello"));
    LXCHECK_TRUE(strings::startsWith("project.pbxproj", "project."));
    LXCHECK_TRUE(strings::endsWith("main.cpp", ".cpp"));
    LXCHECK_EQ(strings::replaceAll("a/b/c", "/", "-"), std::string("a-b-c"));
    LXCHECK_EQ(strings::join({"a", "b", "c"}, ", "), std::string("a, b, c"));

    const auto directory = tempDirectory("core");
    const auto path = directory / "nested" / "file.txt";
    auto written = fs::writeTextAtomic(path, "first", false);
    LXCHECK_TRUE(written);
    LXCHECK_EQ(fs::readText(path).value(), std::string("first"));
    written = fs::writeTextAtomic(path, "second", false);
    LXCHECK_TRUE(written);
    LXCHECK_EQ(fs::readText(path).value(), std::string("second"));
    LXCHECK_EQ(fs::extension("/tmp/Test.Swift"), std::string("Swift"));

    const auto missingText = fs::readText(directory / "does-not-exist");
    LXCHECK_TRUE(!missingText);

    LXCHECK_TRUE(which("definitely-not-a-real-command-xyz").empty());
    auto process = runProcess("sh", {"-c", "printf 'out'; printf 'err' >&2"});
    LXCHECK_EQ(process.exitCode, 0);
    LXCHECK_TRUE(process.stdoutText.find("out") != std::string::npos);

    std::cout << "core tests passed\n";
    return 0;
}
