#pragma once
#include "agentflow/language_server.hpp"
#include "agentflow/model_provider.hpp"
#include <map>
#include <memory>

namespace agentflow {
// Backend-owned language-server tools. The model chooses only a configured
// server, workspace-relative document, LSP operation and cursor position.
class LanguageServerToolRegistry {
public:
    LanguageServerToolRegistry(WorkspaceTools& workspace,std::vector<LanguageServerProfile> profiles);
    ~LanguageServerToolRegistry();
    LanguageServerToolRegistry(const LanguageServerToolRegistry&)=delete;
    LanguageServerToolRegistry& operator=(const LanguageServerToolRegistry&)=delete;
    std::vector<ModelToolDefinition> definitions() const;
    std::string invoke(const std::string& arguments_json,std::stop_token cancel={});
    std::string query(const LanguageServerRequest& request,std::stop_token cancel={});
    const std::vector<LanguageServerProfile>& profiles() const noexcept{return profiles_;}
private:
    WorkspaceTools& workspace_;
    std::vector<LanguageServerProfile> profiles_;
    std::map<std::string,std::unique_ptr<LanguageServerClient>> clients_;
};
}
