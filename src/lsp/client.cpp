#include "lxcode/lsp/client.hpp"

#include "lxcode/core/filesystem.hpp"
#include "lxcode/core/strings.hpp"

#include <algorithm>

namespace lxcode {

std::string languageIdForPath(const std::filesystem::path& path) {
    const auto ext = strings::toLower(fs::extension(path));
    if (ext == "c") return "c";
    if (ext == "h") return "c";
    if (ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "hpp" || ext == "hh") return "cpp";
    if (ext == "m") return "objective-c";
    if (ext == "mm") return "objective-cpp";
    if (ext == "swift") return "swift";
    if (ext == "json") return "json";
    return "plaintext";
}

std::string uriForPath(const std::filesystem::path& path) {
    auto absolute = path.is_absolute() ? path : std::filesystem::absolute(path);
    return "file://" + absolute.lexically_normal().generic_string();
}

std::filesystem::path pathFromUri(const std::string& uri) {
    if (strings::startsWith(uri, "file://")) return uri.substr(7);
    return uri;
}

LspClient::LspClient(std::string languageId, std::string command, std::filesystem::path workspaceRoot)
    : languageId_(std::move(languageId)), command_(std::move(command)),
      workspaceRoot_(std::move(workspaceRoot)) {
    connection_.setMessageHandler([this](const nlohmann::json& message) { onServerMessage(message); });
}

LspClient::~LspClient() { stop(); }

bool LspClient::start() {
    if (command_.empty() || connection_.running()) return false;
    if (!connection_.start(command_, workspaceRoot_)) return false;

    nlohmann::json capabilities;
    capabilities["textDocument"]["completion"]["completionItem"]["snippetSupport"] = false;
    capabilities["textDocument"]["definition"]["linkSupport"] = true;
    capabilities["workspace"]["workspaceFolders"] = true;

    nlohmann::json params;
    params["processId"] = nullptr;
    params["clientInfo"]["name"] = "LXcode";
    params["capabilities"] = capabilities;
    if (!workspaceRoot_.empty()) {
        params["rootUri"] = uriForPath(workspaceRoot_);
        params["workspaceFolders"] = nlohmann::json::array();
        nlohmann::json folder;
        folder["uri"] = uriForPath(workspaceRoot_);
        folder["name"] = workspaceRoot_.filename().string();
        params["workspaceFolders"].push_back(folder);
    }
    auto result = connection_.request("initialize", params, std::chrono::seconds(10));
    if (!result) {
        connection_.stop();
        return false;
    }
    connection_.notify("initialized", nlohmann::json::object());
    return true;
}

void LspClient::stop() { connection_.stop(); }

std::string LspClient::serverName() const {
    const auto first = command_.find_first_not_of(" \t");
    if (first == std::string::npos) return command_;
    const auto last = command_.find_first_of(" \t", first);
    return command_.substr(first, last == std::string::npos ? std::string::npos : last - first);
}

void LspClient::setDiagnosticHandler(
    std::function<void(const std::vector<LspDiagnostic>&)> handler) {
    diagnosticHandler_ = std::move(handler);
}

void LspClient::onServerMessage(const nlohmann::json& message) {
    const auto method = message.value("method", std::string());
    if (method != "textDocument/publishDiagnostics" || !diagnosticHandler_) return;
    const auto& params = message.value("params", nlohmann::json::object());
    std::vector<LspDiagnostic> result;
    for (const auto& item : params.value("diagnostics", nlohmann::json::array())) {
        LspDiagnostic diagnostic;
        diagnostic.file = pathFromUri(params.value("uri", std::string())).string();
        diagnostic.severity = std::to_string(item.value("severity", 1));
        const auto rangeIt = item.find("range");
        if (rangeIt != item.end() && rangeIt->contains("start")) {
            diagnostic.line = rangeIt->at("start").value("line", 0) + 1;
            diagnostic.column = rangeIt->at("start").value("character", 0) + 1;
        }
        diagnostic.message = item.value("message", std::string());
        result.push_back(std::move(diagnostic));
    }
    diagnosticHandler_(result);
}

void LspClient::didOpen(const std::filesystem::path& path, const std::string& text) {
    if (!connection_.running()) return;
    versions_[path.string()] = 1;
    nlohmann::json params;
    params["textDocument"]["uri"] = uriForPath(path);
    params["textDocument"]["languageId"] = languageId_;
    params["textDocument"]["version"] = 1;
    params["textDocument"]["text"] = text;
    connection_.notify("textDocument/didOpen", params);
}

void LspClient::didChange(const std::filesystem::path& path, const std::string& text) {
    if (!connection_.running()) return;
    const int version = ++versions_[path.string()];
    nlohmann::json params;
    params["textDocument"]["uri"] = uriForPath(path);
    params["textDocument"]["version"] = version;
    params["contentChanges"] = nlohmann::json::array();
    nlohmann::json change;
    change["text"] = text;
    params["contentChanges"].push_back(change);
    connection_.notify("textDocument/didChange", params);
}

void LspClient::didClose(const std::filesystem::path& path) {
    if (!connection_.running()) return;
    versions_.erase(path.string());
    nlohmann::json params;
    params["textDocument"]["uri"] = uriForPath(path);
    connection_.notify("textDocument/didClose", params);
}

void LspClient::didSave(const std::filesystem::path& path) {
    if (!connection_.running()) return;
    nlohmann::json params;
    params["textDocument"]["uri"] = uriForPath(path);
    connection_.notify("textDocument/didSave", params);
}

Expected<std::vector<CompletionItem>> LspClient::completion(const std::filesystem::path& path,
                                                            int line, int character) {
    nlohmann::json params;
    params["textDocument"]["uri"] = uriForPath(path);
    params["position"]["line"] = std::max(0, line - 1);
    params["position"]["character"] = std::max(0, character - 1);
    auto response = connection_.request("textDocument/completion", params, std::chrono::seconds(4));
    if (!response) return Expected<std::vector<CompletionItem>>::failure(response.error());
    std::vector<CompletionItem> result;
    const auto& body = *response;
    if (body.contains("items")) {
        for (const auto& item : body["items"]) {
            CompletionItem completion;
            completion.label = item.value("label", std::string());
            completion.insertText = item.value("insertText",
                                               item.value("label", std::string()));
            completion.detail = item.value("detail", std::string());
            completion.kind = item.value("kind", 0);
            result.push_back(std::move(completion));
        }
    } else if (body.is_array()) {
        for (const auto& item : body) {
            CompletionItem completion;
            completion.label = item.value("label", std::string());
            completion.insertText = item.value("insertText", completion.label);
            completion.detail = item.value("detail", std::string());
            result.push_back(std::move(completion));
        }
    }
    return result;
}

Expected<std::vector<LspLocation>> LspClient::definition(const std::filesystem::path& path,
                                                         int line, int character) {
    nlohmann::json params;
    params["textDocument"]["uri"] = uriForPath(path);
    params["position"]["line"] = std::max(0, line - 1);
    params["position"]["character"] = std::max(0, character - 1);
    auto response = connection_.request("textDocument/definition", params, std::chrono::seconds(4));
    if (!response) return Expected<std::vector<LspLocation>>::failure(response.error());
    std::vector<LspLocation> result;
    auto addLocation = [&](const nlohmann::json& item) {
        LspLocation location;
        const std::string uri = item.value("uri", item.value("targetUri", std::string()));
        location.file = pathFromUri(uri).string();
        const auto* range = item.contains("range") ? &item.at("range")
                            : item.contains("targetRange") ? &item.at("targetRange") : nullptr;
        if (range && range->contains("start")) {
            location.line = range->at("start").value("line", 0) + 1;
            location.column = range->at("start").value("character", 0) + 1;
        }
        result.push_back(std::move(location));
    };
    if (response->is_array()) {
        for (const auto& item : *response) addLocation(item);
    } else {
        addLocation(*response);
    }
    return result;
}

Expected<std::string> LspClient::hover(const std::filesystem::path& path, int line, int character) {
    nlohmann::json params;
    params["textDocument"]["uri"] = uriForPath(path);
    params["position"]["line"] = std::max(0, line - 1);
    params["position"]["character"] = std::max(0, character - 1);
    auto response = connection_.request("textDocument/hover", params, std::chrono::seconds(4));
    if (!response) return Expected<std::string>::failure(response.error());
    const auto& contents = response->value("contents", nlohmann::json::object());
    if (contents.is_string()) return contents.get<std::string>();
    if (contents.is_object()) return contents.value("value", std::string());
    if (contents.is_array() && !contents.empty()) {
        const auto& first = contents[0];
        if (first.is_string()) return first.get<std::string>();
        return first.value("value", std::string());
    }
    return std::string();
}

} // namespace lxcode
