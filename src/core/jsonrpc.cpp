#include "lxcode/core/jsonrpc.hpp"

#include <cerrno>
#include <cctype>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <sstream>

namespace lxcode {

namespace {

ssize_t readExact(int fd, char* buffer, std::size_t size) {
    std::size_t total = 0;
    while (total < size) {
        const ssize_t n = ::read(fd, buffer + total, size - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return static_cast<ssize_t>(total);
        total += static_cast<std::size_t>(n);
    }
    return static_cast<ssize_t>(total);
}

} // namespace

JsonRpcConnection::~JsonRpcConnection() { stop(); }

bool JsonRpcConnection::start(const std::string& command, const std::filesystem::path& workingDirectory,
                              const std::map<std::string, std::string>& environment) {
    if (running_) return true;
    int inPipe[2] = {-1, -1};
    int outPipe[2] = {-1, -1};
    int errPipe[2] = {-1, -1};
    if (pipe(inPipe) != 0) return false;
    if (pipe(outPipe) != 0) { ::close(inPipe[0]); ::close(inPipe[1]); return false; }
    if (pipe(errPipe) != 0) {
        ::close(inPipe[0]); ::close(inPipe[1]); ::close(outPipe[0]); ::close(outPipe[1]);
        return false;
    }
    pid_ = fork();
    if (pid_ < 0) {
        ::close(inPipe[0]); ::close(inPipe[1]); ::close(outPipe[0]);
        ::close(outPipe[1]); ::close(errPipe[0]); ::close(errPipe[1]);
        return false;
    }
    if (pid_ == 0) {
        ::setpgid(0, 0);
        ::dup2(inPipe[0], STDIN_FILENO);
        ::dup2(outPipe[1], STDOUT_FILENO);
        ::dup2(errPipe[1], STDERR_FILENO);
        ::close(inPipe[0]); ::close(inPipe[1]);
        ::close(outPipe[0]); ::close(outPipe[1]);
        ::close(errPipe[0]); ::close(errPipe[1]);
        if (!workingDirectory.empty()) ::chdir(workingDirectory.c_str());
        for (const auto& [key, value] : environment) ::setenv(key.c_str(), value.c_str(), 1);
        ::execlp("sh", "sh", "-c", command.c_str(), nullptr);
        ::_exit(127);
    }
    ::close(inPipe[0]);
    ::close(outPipe[1]);
    ::close(errPipe[1]);
    stdinFd_ = inPipe[1];
    stdoutFd_ = outPipe[0];
    stderrFd_ = errPipe[0];
    running_.store(true);
    reader_ = std::thread([this] { readerLoop(); });
    return true;
}

void JsonRpcConnection::stop() {
    if (!running_.exchange(false)) return;
    if (pid_ > 0) {
        ::kill(-pid_, SIGTERM);
        ::kill(pid_, SIGTERM);
    }
    if (stdinFd_ >= 0) { ::close(stdinFd_); stdinFd_ = -1; }
    if (stdoutFd_ >= 0) { ::shutdown(stdoutFd_, SHUT_RDWR); ::close(stdoutFd_); stdoutFd_ = -1; }
    if (stderrFd_ >= 0) { ::close(stderrFd_); stderrFd_ = -1; }
    if (reader_.joinable()) reader_.join();
    if (pid_ > 0) {
        int status = 0;
        ::waitpid(pid_, &status, WNOHANG);
        ::kill(-pid_, SIGKILL);
        ::waitpid(pid_, &status, 0);
        pid_ = -1;
    }
    std::lock_guard<std::mutex> lock(pendingMutex_);
    for (auto& [id, promise] : pending_) {
        try { promise->set_value(nlohmann::json{}); } catch (...) {}
    }
    pending_.clear();
}

void JsonRpcConnection::send(const nlohmann::json& message) {
    if (!running_ || stdinFd_ < 0) return;
    const auto text = message.dump();
    std::lock_guard<std::mutex> lock(writeMutex_);
    const std::string header = "Content-Length: " + std::to_string(text.size()) + "\r\n\r\n";
    std::size_t written = 0;
    while (written < header.size()) {
        const ssize_t n = ::write(stdinFd_, header.data() + written, header.size() - written);
        if (n <= 0) break;
        written += static_cast<std::size_t>(n);
    }
    written = 0;
    while (written < text.size()) {
        const ssize_t n = ::write(stdinFd_, text.data() + written, text.size() - written);
        if (n <= 0) break;
        written += static_cast<std::size_t>(n);
    }
}

void JsonRpcConnection::notify(const std::string& method, nlohmann::json params) {
    nlohmann::json message;
    message["jsonrpc"] = "2.0";
    message["method"] = method;
    message["params"] = std::move(params);
    send(message);
}

Expected<nlohmann::json> JsonRpcConnection::request(const std::string& method, nlohmann::json params,
                                                    std::chrono::milliseconds timeout) {
    const int id = nextId_.fetch_add(1);
    nlohmann::json message;
    message["jsonrpc"] = "2.0";
    message["id"] = id;
    message["method"] = method;
    message["params"] = std::move(params);
    auto response = requestRaw(std::move(message), id, timeout);
    if (!response) return Expected<nlohmann::json>::failure(response.error());
    if (response->contains("error")) {
        const auto& error = (*response)["error"];
        return Expected<nlohmann::json>::failure(
            error.value("message", std::string("request failed")));
    }
    return response;
}

Expected<nlohmann::json> JsonRpcConnection::requestRaw(nlohmann::json message, int id,
                                                       std::chrono::milliseconds timeout) {
    if (!running_) return Expected<nlohmann::json>::failure("JSON-RPC connection is not running");
    auto promise = std::make_shared<std::promise<nlohmann::json>>();
    auto future = promise->get_future();
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pending_[id] = promise;
    }
    send(message);
    const std::string command = message.value("method", message.value("command", std::string("request")));
    if (future.wait_for(timeout) != std::future_status::ready) {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pending_.erase(id);
        return Expected<nlohmann::json>::failure("timed out waiting for " + command);
    }
    auto response = future.get();
    if (response.contains("success") && !response["success"].get<bool>()) {
        const auto& body = response.value("body", nlohmann::json::object());
        if (body.contains("error") && body["error"].is_object())
            return Expected<nlohmann::json>::failure(
                body["error"].value("message", std::string("debugger request failed")));
        return Expected<nlohmann::json>::failure(body.value("message", std::string("debugger request failed")));
    }
    return response;
}

void JsonRpcConnection::setMessageHandler(std::function<void(const nlohmann::json&)> handler) {
    handler_ = std::move(handler);
}

void JsonRpcConnection::readerLoop() {
    while (running_ && stdoutFd_ >= 0) {
        std::string line;
        bool headerComplete = false;
        std::size_t contentLength = 0;
        while (running_ && stdoutFd_ >= 0) {
            char c = 0;
            if (readExact(stdoutFd_, &c, 1) != 1) { running_.store(false); return; }
            if (c == '\r') continue;
            if (c == '\n') {
                if (line.empty()) { headerComplete = true; break; }
                const auto colon = line.find(':');
                if (colon != std::string::npos) {
                    auto name = line.substr(0, colon);
                    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
                        return static_cast<char>(std::tolower(ch));
                    });
                    if (name == "content-length") {
                        try { contentLength = std::stoul(line.substr(colon + 1)); } catch (...) {}
                    }
                }
                line.clear();
            } else {
                line.push_back(c);
            }
        }
        if (!headerComplete || !running_) return;
        if (contentLength == 0 || contentLength > 32 * 1024 * 1024) continue;
        std::string body(contentLength, '\0');
        if (readExact(stdoutFd_, body.data(), body.size()) != static_cast<ssize_t>(body.size())) {
            running_.store(false);
            return;
        }
        try {
            handle(nlohmann::json::parse(body));
        } catch (...) {
            // Malformed server output is ignored; the connection stays usable.
        }
    }
}

void JsonRpcConnection::handle(const nlohmann::json& message) {
    if (!message.is_object()) return;
    const bool isResponse = message.contains("request_seq") ||
                            (message.contains("id") && (message.contains("result") ||
                                                        message.contains("error")));
    if (isResponse) {
        const int id = message.contains("request_seq")
                           ? message["request_seq"].get<int>()
                           : message["id"].get<int>();
        std::shared_ptr<std::promise<nlohmann::json>> promise;
        {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            auto it = pending_.find(id);
            if (it != pending_.end()) {
                promise = it->second;
                pending_.erase(it);
            }
        }
        if (promise) {
            try { promise->set_value(message); } catch (...) {}
        }
        return;
    }
    if (message.contains("method") && handler_) handler_(message);
}

} // namespace lxcode
