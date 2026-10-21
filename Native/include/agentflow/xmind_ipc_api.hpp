#pragma once
#include <memory>
#include <string>
namespace agentflow {
class PersistenceService;
class RunExecutor;
class BackendOwnerControl;
// Owns the xlang3 root export and listener. Construct after backend services;
// destroy before those services. No transport-specific state belongs in them.
class XMindIpcServer final {
public:
    XMindIpcServer(PersistenceService&,RunExecutor*,const std::string& local_auth_token,BackendOwnerControl* owner=nullptr);
    ~XMindIpcServer();
    XMindIpcServer(const XMindIpcServer&)=delete;
    XMindIpcServer& operator=(const XMindIpcServer&)=delete;
    const std::string& endpoint()const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
