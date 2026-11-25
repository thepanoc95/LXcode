#pragma once
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <type_traits>
#include <utility>
#include <string>

namespace lxcode::test {

inline std::filesystem::path tempDirectory(const std::string& name) {
    auto path = std::filesystem::temp_directory_path() / "lxcode-tests" / name;
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    std::filesystem::create_directories(path, ec);
    return path;
}

inline void writeFile(const std::filesystem::path& path, const std::string& contents) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    auto file = std::fopen(path.c_str(), "wb");
    if (!file) return;
    std::fwrite(contents.data(), 1, contents.size(), file);
    std::fclose(file);
}

inline std::string readFile(const std::filesystem::path& path) {
    auto file = std::fopen(path.c_str(), "rb");
    if (!file) return {};
    std::string result;
    char buffer[4096];
    std::size_t count = 0;
    while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0) result.append(buffer, count);
    std::fclose(file);
    return result;
}

} // namespace lxcode::test

#define LXCHECK(condition)                                                                        \
    do {                                                                                          \
        if (!(condition)) {                                                                       \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';    \
            return 1;                                                                             \
        }                                                                                         \
    } while (false)

namespace lxcode::test::detail {

template <class T, class = void>
struct Streamable : std::false_type {};

template <class T>
struct Streamable<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>>
    : std::true_type {};

template <class T>
std::string describe(const T& value) {
    if constexpr (Streamable<T>::value) {
        std::ostringstream stream;
        stream << value;
        return stream.str();
    } else {
        return "<non-streamable>";
    }
}

} // namespace lxcode::test::detail

#define LXCHECK_EQ(actual, expected)                                                              \
    do {                                                                                          \
        const auto& actualValue = (actual);                                                       \
        const auto& expectedValue = (expected);                                                   \
        if (!(actualValue == expectedValue)) {                                                    \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " << #actual                \
                      << " == " << #expected << " (" << ::lxcode::test::detail::describe(actualValue) \
                      << " != " << ::lxcode::test::detail::describe(expectedValue) << ")\n";      \
            return 1;                                                                             \
        }                                                                                         \
    } while (false)

#define LXCHECK_TRUE(value) LXCHECK(static_cast<bool>(value))
