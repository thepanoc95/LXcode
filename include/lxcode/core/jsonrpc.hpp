#pragma once
#include "lxcode/core/expected.hpp"

#include <nlohmann/json.hpp>

#include <sys/types.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace lxcode {

// Length-framed JSON-RPC over stdin/stdout. Used by both LSP and DAP servers.
class JsonRpcConnection {
public:
    JsonRpcConnection() = default;
    ~JsonRpcConnection();
    JsonRpcConnection(const JsonRpcConnection&) = delete;
    JsonRpcConnection& operator=(const JsonRpcConnection&) = delete;

    bool start(const std::string& command, const std::filesystem::path& workingDirectory = {},
               const std::map<std::string, std::string>& environment = {});
    void stop();
    bool running() const { return running_.load(); }

    void send(const nlohmann::json& message);
    void notify(const std::string& method, nlohmann::json params);
    Expected<nlohmann::json> request(const std::string& method, nlohmann::json params,
                                     std::chrono::milliseconds timeout = std::chrono::seconds(5));
    // Sends an already-formed request object (DAP uses a different envelope).
    Expected<nlohmann::json> requestRaw(nlohmann::json message, int id,
                                        std::chrono::milliseconds timeout = std::chrono::seconds(5));
    void setMessageHandler(std::function<void(const nlohmann::json&)> handler);

private:
    void readerLoop();
    void handle(const nlohmann::json& message);

    int stdinFd_ = -1;
    int stdoutFd_ = -1;
    int stderrFd_ = -1;
    pid_t pid_ = -1;
    std::thread reader_;
    std::atomic_bool running_{false};
    std::atomic<int> nextId_{1};
    std::mutex writeMutex_;
    std::mutex pendingMutex_;
    std::map<int, std::shared_ptr<std::promise<nlohmann::json>>> pending_;
    std::function<void(const nlohmann::json&)> handler_;
};

} // namespace lxcode
