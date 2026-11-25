#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace lxcode {

struct ProcessResult {
    int exitCode = -1;
    std::string stdoutText;
    std::string stderrText;
    bool timedOut = false;
    bool cancelled = false;
    bool spawned = false;
};

struct ProcessOptions {
    std::string workingDirectory;
    std::map<std::string, std::string> environment;
    std::chrono::milliseconds timeout{0}; // 0 means no timeout
    const std::atomic_bool* cancelFlag = nullptr;
    std::function<void(const std::string&, bool)> onOutput; // (chunk, isStderr)
    bool mergeStreams = true;
};

// Runs a command synchronously, with optional timeout/cancellation. This is
// used from worker threads to keep the GUI responsive.
ProcessResult runProcess(const std::string& executable,
                         const std::vector<std::string>& arguments,
                         const ProcessOptions& options = {});

class AsyncProcess {
public:
    AsyncProcess(std::string executable, std::vector<std::string> arguments,
                 ProcessOptions options = {});
    ~AsyncProcess();

    AsyncProcess(const AsyncProcess&) = delete;
    AsyncProcess& operator=(const AsyncProcess&) = delete;

    void start(std::function<void(const ProcessResult&)> onFinished);
    bool running() const;
    void cancel();
    ProcessResult wait();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// True when `name` resolves via PATH.
bool executableExists(const std::string& name);
std::string which(const std::string& name);

} // namespace lxcode
