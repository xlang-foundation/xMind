#pragma once
#include "agentflow/http_stream_transport.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace agentflow {
struct A2aRemoteAgentInfo {
    std::string name,description,agent_version,protocol_version,endpoint,skills_json;
};
struct A2aRemoteError : std::runtime_error {
    int code;
    explicit A2aRemoteError(int value):std::runtime_error("Remote A2A request failed (JSON-RPC code "+std::to_string(value)+")"),code(value){}
};
// Native JSON-RPC client for a single owner-configured AgentCard URL. It only
// returns peer result JSON; callers must treat it as untrusted data and own
// durable admission, permission and uncertain-effect recovery.
class A2aJsonRpcClient {
public:
    A2aJsonRpcClient(std::string card_url,std::optional<SecretBytes> bearer={});
    ~A2aJsonRpcClient();
    A2aJsonRpcClient(const A2aJsonRpcClient&)=delete;
    A2aJsonRpcClient& operator=(const A2aJsonRpcClient&)=delete;
    A2aRemoteAgentInfo discover(std::chrono::milliseconds timeout,std::stop_token cancel={});
    std::string send_message(std::string_view params_json,std::chrono::milliseconds timeout,std::stop_token cancel={},std::function<void()> on_sending={});
    std::string get_task(std::string_view params_json,std::chrono::milliseconds timeout,std::stop_token cancel={});
    std::string cancel_task(std::string_view params_json,std::chrono::milliseconds timeout,std::stop_token cancel={});
    bool ready()const;
    A2aRemoteAgentInfo remote()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
