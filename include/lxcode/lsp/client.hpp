#pragma once
#include "lxcode/core/expected.hpp"
#include "lxcode/core/jsonrpc.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace lxcode {

struct LspDiagnostic {
    std::string file;
    int line = 0;
    int column = 0;
    std::string severity;
    std::string message;
};

struct CompletionItem {
    std::string label;
    std::string insertText;
    std::string detail;
    int kind = 0;
};

struct LspLocation {
    std::string file;
    int line = 0;
    int column = 0;
};

std::string languageIdForPath(const std::filesystem::path& path);
std::string uriForPath(const std::filesystem::path& path);
std::filesystem::path pathFromUri(const std::string& uri);

class LspClient {
public:
    LspClient(std::string languageId, std::string command,
              std::filesystem::path workspaceRoot = {});
    ~LspClient();

    bool start();
    void stop();
    bool running() const { return connection_.running(); }
    std::string serverName() const;

    void setDiagnosticHandler(std::function<void(const std::vector<LspDiagnostic>&)> handler);

    void didOpen(const std::filesystem::path& path, const std::string& text);
    void didChange(const std::filesystem::path& path, const std::string& text);
    void didClose(const std::filesystem::path& path);
    void didSave(const std::filesystem::path& path);

    Expected<std::vector<CompletionItem>> completion(const std::filesystem::path& path,
                                                     int line, int character);
    Expected<std::vector<LspLocation>> definition(const std::filesystem::path& path,
                                                  int line, int character);
    Expected<std::string> hover(const std::filesystem::path& path, int line, int character);

private:
    void onServerMessage(const nlohmann::json& message);

    std::string languageId_;
    std::string command_;
    std::filesystem::path workspaceRoot_;
    JsonRpcConnection connection_;
    std::map<std::string, int> versions_;
    std::function<void(const std::vector<LspDiagnostic>&)> diagnosticHandler_;
};

} // namespace lxcode
