#pragma once
#include "agentflow/mcp_stdio.hpp"
#include "agentflow/workspace_tools.hpp"
#include <cstdint>
#include <string>
#include <memory>
#include <optional>
#include <vector>

namespace agentflow {
struct LanguageServerProfile {
    std::string id,executable,executable_id,language_id;
    std::int64_t revision=0;
    std::vector<std::string> extensions,arguments;
    std::vector<std::pair<std::string,std::string>> environment;
    std::string environment_credential_id;
    std::chrono::milliseconds request_timeout{30000};
};
struct LanguageServerRequest {
    std::string path,method;
    std::size_t line=0,character=0;
    std::optional<std::string> document_text;
    struct FormattingOptions {
        std::size_t tab_size=4;bool insert_spaces=true;
        std::optional<bool> trim_trailing_whitespace,insert_final_newline,trim_final_newlines;
    };
    std::optional<FormattingOptions> formatting_options;
};
// A bounded, one-request native LSP client. The server executable, arguments,
// environment and file-extension binding come only from trusted backend
// configuration. The model supplies a workspace-relative document and method.
class LanguageServerClient {
public:
    LanguageServerClient(WorkspaceTools& workspace,LanguageServerProfile profile);
    ~LanguageServerClient();
    LanguageServerClient(const LanguageServerClient&)=delete;
    LanguageServerClient& operator=(const LanguageServerClient&)=delete;
    std::string request(const LanguageServerRequest& request,std::stop_token cancel={}) const;
private:
    struct Impl;
    WorkspaceTools& workspace_;
    LanguageServerProfile profile_;
    std::unique_ptr<Impl> impl_;
};
}
