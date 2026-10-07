#pragma once
#include "agentflow/persistence_service.hpp"
#include "agentflow/run_executor.hpp"
#include <string_view>

namespace agentflow {
void validate_local_auth_token(std::string_view token);
// Loopback transport adapter. No HTTP/Electron/WebRTC dependencies in core.
class HttpServer {
public:
    HttpServer(PersistenceService& persistence,std::string auth_token,RunExecutor* executor=nullptr);
    ~HttpServer();
    HttpServer(const HttpServer&)=delete;
    HttpServer& operator=(const HttpServer&)=delete;
    int bind(int port); // 0 selects an available loopback port.
    bool listen();
    void stop();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
