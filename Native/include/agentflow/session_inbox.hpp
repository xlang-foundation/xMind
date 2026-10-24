#pragma once
#include "agentflow/run_executor.hpp"
#include "agentflow/records.hpp"
#include <memory>
namespace agentflow {
class PersistenceService;class BackendOwnerControl;class ProviderSetup;class ProviderProfileSetup;
struct SessionInboxRequest {std::string id,content;std::vector<PromptFileAttachment> files;std::optional<WorkspaceAdmission> workspace;};
// One durable queue dispatcher per backend owner; transport adapters borrow it.
class SessionInboxService final {
public:
    SessionInboxService(PersistenceService&,RunExecutor*,BackendOwnerControl* = nullptr,ProviderSetup* = nullptr,ProviderProfileSetup* = nullptr);
    ~SessionInboxService();
    SessionInboxService(const SessionInboxService&)=delete;
    SessionInboxService& operator=(const SessionInboxService&)=delete;
    std::vector<SessionInboxItem> list(const std::string& session)const;
    SessionInboxItem enqueue(const std::string& session,SessionInboxRequest request);
    void cancel(const std::string& session,const std::string& id);
    void start();
    void close();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
