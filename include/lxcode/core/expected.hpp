#pragma once
#include <optional>
#include <string>
#include <utility>

namespace lxcode {

// A small result type compatible with C++20. Kept local so the core library has
// no dependency on a particular expected/variant implementation.
template <class T> class Expected {
public:
    Expected() = default;
    Expected(T value) : value_(std::move(value)) {}

    static Expected failure(std::string error) {
        Expected result;
        result.error_ = std::move(error);
        return result;
    }

    bool ok() const { return value_.has_value(); }
    explicit operator bool() const { return ok(); }
    const T& value() const { return *value_; }
    T& value() { return *value_; }
    const T& operator*() const { return *value_; }
    T& operator*() { return *value_; }
    const T* operator->() const { return &*value_; }
    T* operator->() { return &*value_; }
    const std::string& error() const { return error_; }

private:
    std::optional<T> value_;
    std::string error_;
};

template <> class Expected<void> {
public:
    Expected() = default;
    Expected(const char* error) : error_(error) {}
    Expected(std::string error) : error_(std::move(error)) {}

    static Expected failure(std::string error) { return Expected(std::move(error)); }

    bool ok() const { return error_.empty(); }
    explicit operator bool() const { return ok(); }
    const std::string& error() const { return error_; }

private:
    std::string error_;
};

} // namespace lxcode
