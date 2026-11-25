#include "lxcode/core/process.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <future>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <sstream>

namespace lxcode {

namespace {
void setNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void closeFd(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}
} // namespace

ProcessResult runProcess(const std::string& executable,
                         const std::vector<std::string>& arguments,
                         const ProcessOptions& options) {
    ProcessResult result;
    int outPipe[2] = {-1, -1};
    int errPipe[2] = {-1, -1};

    if (pipe(outPipe) != 0) {
        result.stderrText = "pipe() failed: " + std::string(strerror(errno));
        return result;
    }
    if (!options.mergeStreams && pipe(errPipe) != 0) {
        closeFd(outPipe[0]);
        closeFd(outPipe[1]);
        result.stderrText = "pipe() failed: " + std::string(strerror(errno));
        return result;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        closeFd(outPipe[0]);
        closeFd(outPipe[1]);
        closeFd(errPipe[0]);
        closeFd(errPipe[1]);
        result.stderrText = "fork() failed: " + std::string(strerror(errno));
        return result;
    }

    if (pid == 0) {
        ::setpgid(0, 0);
        ::dup2(outPipe[1], STDOUT_FILENO);
        if (options.mergeStreams)
            ::dup2(outPipe[1], STDERR_FILENO);
        else
            ::dup2(errPipe[1], STDERR_FILENO);
        closeFd(outPipe[0]);
        closeFd(outPipe[1]);
        closeFd(errPipe[0]);
        closeFd(errPipe[1]);

        if (!options.workingDirectory.empty())
            ::chdir(options.workingDirectory.c_str());
        for (const auto& [key, value] : options.environment)
            ::setenv(key.c_str(), value.c_str(), 1);

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(executable.c_str()));
        for (const auto& arg : arguments)
            argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        ::execvp(executable.c_str(), argv.data());
        const std::string message = "cannot execute '" + executable + "': " + strerror(errno) + "\n";
        ::write(STDERR_FILENO, message.data(), message.size());
        ::_exit(127);
    }

    result.spawned = true;
    closeFd(outPipe[1]);
    closeFd(errPipe[1]);
    setNonBlocking(outPipe[0]);
    if (!options.mergeStreams) setNonBlocking(errPipe[0]);

    struct pollfd fds[2] = {{outPipe[0], POLLIN, 0}, {errPipe[0], POLLIN, 0}};
    nfds_t count = options.mergeStreams ? 1 : 2;

    const auto start = std::chrono::steady_clock::now();
    bool childExited = false;
    int status = 0;
    char buffer[8192];

    while (count > 0) {
        if (options.cancelFlag && options.cancelFlag->load()) {
            result.cancelled = true;
            ::kill(-pid, SIGKILL);
            break;
        }
        if (options.timeout.count() > 0) {
            const auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed >= options.timeout) {
                result.timedOut = true;
                ::kill(-pid, SIGKILL);
                break;
            }
        }

        const int ready = ::poll(fds, count, 25);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }

        for (nfds_t i = 0; i < count; ++i) {
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            const ssize_t n = ::read(fds[i].fd, buffer, sizeof(buffer));
            if (n > 0) {
                const std::string chunk(buffer, static_cast<std::size_t>(n));
                const bool isErr = (!options.mergeStreams && i == 1);
                if (isErr)
                    result.stderrText.append(chunk);
                else if (options.mergeStreams)
                    result.stdoutText.append(chunk); // merged output kept in stdoutText
                else
                    result.stdoutText.append(chunk);
                if (options.onOutput) options.onOutput(chunk, isErr);
            } else if (n == 0 || (fds[i].revents & (POLLHUP | POLLERR))) {
                closeFd(fds[i].fd);
                fds[i].fd = -1;
            }
        }

        // Compact closed descriptors from the poll set.
        nfds_t writeIndex = 0;
        for (nfds_t i = 0; i < count; ++i) {
            if (fds[i].fd >= 0) fds[writeIndex++] = fds[i];
        }
        count = writeIndex;

        const auto waited = ::waitpid(pid, &status, WNOHANG);
        if (waited == pid) childExited = true;
    }

    if (!childExited) {
        if (options.cancelFlag && options.cancelFlag->load()) {
            result.cancelled = true;
            ::kill(-pid, SIGKILL);
        } else if (options.timeout.count() > 0 &&
                   std::chrono::steady_clock::now() - start >= options.timeout) {
            result.timedOut = true;
            ::kill(-pid, SIGKILL);
        }
        // Drain remaining output before waiting so the child cannot block.
        while (true) {
            const ssize_t n = ::read(outPipe[0], buffer, sizeof(buffer));
            if (n <= 0) break;
            const std::string chunk(buffer, static_cast<std::size_t>(n));
            result.stdoutText.append(chunk);
            if (options.onOutput) options.onOutput(chunk, false);
        }
        ::waitpid(pid, &status, 0);
    }

    closeFd(outPipe[0]);
    closeFd(errPipe[0]);

    if (WIFEXITED(status)) result.exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) result.exitCode = 128 + WTERMSIG(status);
    if (result.cancelled) result.exitCode = 130;
    if (result.timedOut) result.exitCode = 124;
    return result;
}

struct AsyncProcess::Impl {
    std::string executable;
    std::vector<std::string> arguments;
    ProcessOptions options;
    std::shared_ptr<std::atomic_bool> cancelFlag = std::make_shared<std::atomic_bool>(false);
    std::future<ProcessResult> future;
    std::function<void(const ProcessResult&)> callback;
};

AsyncProcess::AsyncProcess(std::string executable, std::vector<std::string> arguments,
                           ProcessOptions options)
    : impl_(std::make_unique<Impl>()) {
    impl_->executable = std::move(executable);
    impl_->arguments = std::move(arguments);
    impl_->options = std::move(options);
}

AsyncProcess::~AsyncProcess() {
    cancel();
    if (impl_->future.valid()) impl_->future.wait();
}

void AsyncProcess::start(std::function<void(const ProcessResult&)> onFinished) {
    if (impl_->future.valid()) return;
    impl_->callback = std::move(onFinished);
    impl_->options.cancelFlag = impl_->cancelFlag.get();
    auto exe = impl_->executable;
    auto args = impl_->arguments;
    auto opts = impl_->options;
    auto callback = impl_->callback;
    auto flag = impl_->cancelFlag;
    impl_->future = std::async(std::launch::async, [exe, args, opts, callback, flag]() {
        auto local = opts;
        local.cancelFlag = flag.get();
        ProcessResult result = runProcess(exe, args, local);
        if (callback) callback(result);
        return result;
    });
}

bool AsyncProcess::running() const {
    return impl_->future.valid() &&
           impl_->future.wait_for(std::chrono::seconds(0)) == std::future_status::timeout;
}

void AsyncProcess::cancel() { impl_->cancelFlag->store(true); }

ProcessResult AsyncProcess::wait() {
    if (!impl_->future.valid()) return {};
    return impl_->future.get();
}

bool executableExists(const std::string& name) { return !which(name).empty(); }

std::string which(const std::string& name) {
    if (name.find('/') != std::string::npos) {
        return ::access(name.c_str(), X_OK) == 0 ? name : std::string();
    }
    const char* path = ::getenv("PATH");
    if (!path) return {};
    std::istringstream stream(path);
    std::string directory;
    while (std::getline(stream, directory, ':')) {
        if (directory.empty()) directory = ".";
        const std::string candidate = directory + "/" + name;
        if (::access(candidate.c_str(), X_OK) == 0) return candidate;
    }
    return {};
}

} // namespace lxcode
