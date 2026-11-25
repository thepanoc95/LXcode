#include "lxcode/debug/dapclient.hpp"

#include <atomic>

namespace lxcode {

DapClient::DapClient(std::string command, std::filesystem::path workingDirectory)
    : command_(std::move(command)), workingDirectory_(std::move(workingDirectory)) {
    connection_.setMessageHandler([this](const nlohmann::json& message) { onMessage(message); });
}

DapClient::~DapClient() { stop(); }

bool DapClient::start() {
    if (command_.empty()) return false;
    return connection_.start(command_, workingDirectory_);
}

void DapClient::stop() {
    if (connection_.running()) {
        nlohmann::json message;
        message["seq"] = 0;
        message["type"] = "request";
        message["command"] = "disconnect";
        message["arguments"] = nlohmann::json::object();
        connection_.send(message);
    }
    connection_.stop();
}

void DapClient::setStoppedHandler(std::function<void(const nlohmann::json&)> handler) {
    stoppedHandler_ = std::move(handler);
}

void DapClient::setOutputHandler(std::function<void(const std::string&)> handler) {
    outputHandler_ = std::move(handler);
}

void DapClient::setBreakpointHandler(std::function<void(const DapBreakpoint&)> handler) {
    breakpointHandler_ = std::move(handler);
}

Expected<nlohmann::json> DapClient::sendRequest(const std::string& command,
                                                nlohmann::json arguments) {
    static std::atomic<int> sequence{1};
    const int id = sequence.fetch_add(1);
    nlohmann::json message;
    message["seq"] = id;
    message["type"] = "request";
    message["command"] = command;
    message["arguments"] = std::move(arguments);
    auto response = connection_.requestRaw(message, id, std::chrono::seconds(10));
    if (!response) return Expected<nlohmann::json>::failure(response.error());
    const auto& body = response->value("body", nlohmann::json::object());
    return body;
}

Expected<void> DapClient::sendCommand(const std::string& command, nlohmann::json arguments) {
    auto result = sendRequest(command, std::move(arguments));
    return result ? Expected<void>() : Expected<void>::failure(result.error());
}

void DapClient::sendEvent(const std::string& event, nlohmann::json body) {
    static std::atomic<int> sequence{1000000};
    nlohmann::json message;
    message["seq"] = sequence.fetch_add(1);
    message["type"] = "event";
    message["event"] = event;
    message["body"] = std::move(body);
    connection_.send(message);
}

void DapClient::onMessage(const nlohmann::json& message) {
    const auto type = message.value("type", std::string());
    const auto event = message.value("event", std::string());
    if (type == "event" && event == "stopped" && stoppedHandler_)
        stoppedHandler_(message.value("body", nlohmann::json::object()));
    if (type == "event" && event == "output" && outputHandler_) {
        const auto body = message.value("body", nlohmann::json::object());
        outputHandler_(body.value("output", std::string()));
    }
    if (type == "event" && event == "breakpoint" && breakpointHandler_) {
        const auto& body = message.value("body", nlohmann::json::object());
        DapBreakpoint breakpoint;
        breakpoint.id = body.value("id", 0);
        breakpoint.verified = body.value("verified", false);
        breakpointHandler_(breakpoint);
    }
}

Expected<void> DapClient::initialize(const std::string& adapterId) {
    nlohmann::json arguments;
    arguments["clientID"] = "lxcode";
    arguments["adapterID"] = adapterId;
    arguments["linesStartAt1"] = true;
    arguments["columnsStartAt1"] = true;
    arguments["pathFormat"] = "path";
    arguments["supportsVariableType"] = true;
    auto result = sendRequest("initialize", arguments);
    if (!result) return Expected<void>::failure(result.error());
    capabilities_ = *result;
    sendEvent("initialized", nlohmann::json::object());
    return {};
}

Expected<void> DapClient::launch(const std::filesystem::path& program,
                                 const std::vector<std::string>& arguments,
                                 const std::filesystem::path& cwd) {
    nlohmann::json args;
    args["program"] = program.string();
    args["args"] = arguments;
    if (!cwd.empty()) args["cwd"] = cwd.string();
    args["console"] = "internalConsole";
    return sendCommand("launch", args);
}

Expected<void> DapClient::attach(int pid) {
    nlohmann::json args;
    args["pid"] = pid;
    return sendCommand("attach", args);
}

Expected<void> DapClient::setBreakpoints(const std::filesystem::path& file,
                                         const std::vector<int>& lines) {
    nlohmann::json args;
    args["source"]["path"] = file.string();
    args["breakpoints"] = nlohmann::json::array();
    for (int line : lines) {
        nlohmann::json breakpoint;
        breakpoint["line"] = line;
        args["breakpoints"].push_back(breakpoint);
    }
    auto result = sendRequest("setBreakpoints", args);
    if (!result) return Expected<void>::failure(result.error());
    if (breakpointHandler_ && result->contains("breakpoints")) {
        for (const auto& item : (*result)["breakpoints"]) {
            DapBreakpoint breakpoint;
            breakpoint.id = item.value("id", 0);
            breakpoint.verified = item.value("verified", false);
            breakpoint.file = file.string();
            breakpoint.line = item.value("line", 0);
            breakpointHandler_(breakpoint);
        }
    }
    return {};
}

Expected<void> DapClient::configurationDone() { return sendCommand("configurationDone"); }

Expected<void> DapClient::continueExecution(int threadId) {
    nlohmann::json args;
    args["threadId"] = threadId;
    return sendCommand("continue", args);
}

Expected<void> DapClient::pause(int threadId) {
    nlohmann::json args;
    args["threadId"] = threadId;
    return sendCommand("pause", args);
}

Expected<void> DapClient::next(int threadId) {
    nlohmann::json args;
    args["threadId"] = threadId;
    return sendCommand("next", args);
}

Expected<void> DapClient::stepIn(int threadId) {
    nlohmann::json args;
    args["threadId"] = threadId;
    return sendCommand("stepIn", args);
}

Expected<void> DapClient::stepOut(int threadId) {
    nlohmann::json args;
    args["threadId"] = threadId;
    return sendCommand("stepOut", args);
}

Expected<std::vector<DapThread>> DapClient::threads() {
    auto result = sendRequest("threads");
    if (!result) return Expected<std::vector<DapThread>>::failure(result.error());
    std::vector<DapThread> threads;
    for (const auto& item : result->value("threads", nlohmann::json::array())) {
        DapThread thread;
        thread.id = item.value("id", 0);
        thread.name = item.value("name", std::string());
        threads.push_back(std::move(thread));
    }
    return threads;
}

Expected<std::vector<DapStackFrame>> DapClient::stackTrace(int threadId) {
    nlohmann::json args;
    args["threadId"] = threadId;
    auto result = sendRequest("stackTrace", args);
    if (!result) return Expected<std::vector<DapStackFrame>>::failure(result.error());
    std::vector<DapStackFrame> frames;
    for (const auto& item : result->value("stackFrames", nlohmann::json::array())) {
        DapStackFrame frame;
        frame.id = item.value("id", 0);
        frame.name = item.value("name", std::string());
        if (item.contains("source"))
            frame.file = item["source"].value("path", std::string());
        frame.line = item.value("line", 0);
        frames.push_back(std::move(frame));
    }
    return frames;
}

Expected<std::vector<DapVariable>> DapClient::variables(int variablesReference) {
    nlohmann::json args;
    args["variablesReference"] = variablesReference;
    auto result = sendRequest("variables", args);
    if (!result) return Expected<std::vector<DapVariable>>::failure(result.error());
    std::vector<DapVariable> variables;
    for (const auto& item : result->value("variables", nlohmann::json::array())) {
        DapVariable variable;
        variable.name = item.value("name", std::string());
        variable.value = item.value("value", std::string());
        variable.type = item.value("type", std::string());
        variable.variablesReference = item.value("variablesReference", 0);
        variables.push_back(std::move(variable));
    }
    return variables;
}

Expected<std::string> DapClient::evaluate(const std::string& expression, int frameId) {
    nlohmann::json args;
    args["expression"] = expression;
    args["frameId"] = frameId;
    args["context"] = "watch";
    auto result = sendRequest("evaluate", args);
    if (!result) return Expected<std::string>::failure(result.error());
    return result->value("result", std::string());
}

Expected<void> DapClient::disconnect() {
    nlohmann::json args;
    args["terminateDebuggee"] = false;
    return sendCommand("disconnect", args);
}

} // namespace lxcode
