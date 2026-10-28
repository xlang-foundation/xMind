#pragma once
#include <memory>
#include <string>
#include <functional>
namespace httplib { struct Request; struct Response; }
namespace agentflow {
class PersistenceService;
class RunExecutor;
class BackendOwnerControl;
class LocalConnectionBindings;
// Register the process root independently of public backend service binding.
void ensure_xmind_ipc_api();
// Binds public backend services to the process root and closes their admission
// before backend teardown. The SDK runtime/listener is shared process-wide.
class XMindIpcServer final {
public:
    XMindIpcServer(PersistenceService&,RunExecutor*,const std::string& local_auth_token,BackendOwnerControl* owner=nullptr);
    ~XMindIpcServer();
    XMindIpcServer(const XMindIpcServer&)=delete;
    XMindIpcServer& operator=(const XMindIpcServer&)=delete;
    LocalConnectionBindings& connection_bindings()noexcept;
    const std::string& endpoint()const noexcept;
    void bind_web_requests(std::function<void(httplib::Request&,httplib::Response&)>,int port,std::function<bool()> running={});
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
