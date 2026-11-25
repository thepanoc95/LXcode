#pragma once
#include "lxcode/core/expected.hpp"
#include "lxcode/core/jsonrpc.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace lxcode {

struct DapThread {
    int id = 0;
    std::string name;
};

struct DapStackFrame {
    int id = 0;
    std::string name;
    std::string file;
    int line = 0;
};

struct DapVariable {
    std::string name;
    std::string value;
    std::string type;
    int variablesReference = 0;
};

struct DapBreakpoint {
    int id = 0;
    std::string file;
    int line = 0;
    bool verified = false;
};

class DapClient {
public:
    DapClient(std::string command, std::filesystem::path workingDirectory = {});
    ~DapClient();

    bool start();
    void stop();
    bool running() const { return connection_.running(); }
    const nlohmann::json& capabilities() const { return capabilities_; }

    void setStoppedHandler(std::function<void(const nlohmann::json&)> handler);
    void setOutputHandler(std::function<void(const std::string&)> handler);
    void setBreakpointHandler(std::function<void(const DapBreakpoint&)> handler);

    Expected<void> initialize(const std::string& adapterId = "lxcode");
    Expected<void> launch(const std::filesystem::path& program,
                          const std::vector<std::string>& arguments = {},
                          const std::filesystem::path& cwd = {});
    Expected<void> attach(int pid);
    Expected<void> setBreakpoints(const std::filesystem::path& file,
                                  const std::vector<int>& lines);
    Expected<void> configurationDone();
    Expected<void> continueExecution(int threadId);
    Expected<void> pause(int threadId);
    Expected<void> next(int threadId);
    Expected<void> stepIn(int threadId);
    Expected<void> stepOut(int threadId);
    Expected<std::vector<DapThread>> threads();
    Expected<std::vector<DapStackFrame>> stackTrace(int threadId);
    Expected<std::vector<DapVariable>> variables(int variablesReference);
    Expected<std::string> evaluate(const std::string& expression, int frameId);
    Expected<void> disconnect();

private:
    Expected<nlohmann::json> sendRequest(const std::string& command, nlohmann::json arguments = {});
    Expected<void> sendCommand(const std::string& command, nlohmann::json arguments = {});
    void sendEvent(const std::string& event, nlohmann::json body = {});
    void onMessage(const nlohmann::json& message);

    std::string command_;
    std::filesystem::path workingDirectory_;
    JsonRpcConnection connection_;
    nlohmann::json capabilities_;
    std::function<void(const nlohmann::json&)> stoppedHandler_;
    std::function<void(const std::string&)> outputHandler_;
    std::function<void(const DapBreakpoint&)> breakpointHandler_;
};

} // namespace lxcode
