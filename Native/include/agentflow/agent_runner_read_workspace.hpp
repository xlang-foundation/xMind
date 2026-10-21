#pragma once
#include "agentflow/agent_read_tool_host.hpp"
#include "agentflow/delegation_records.hpp"
namespace agentflow {
// An admitted ordinary read workspace. All values and the read capability are
// owned; native roots, guidance objects and persistence stay at the backend.
// Additional coding effects require their own admitted owner ports.
class AgentRunnerReadWorkspace {
public:
    virtual ~AgentRunnerReadWorkspace()=default;
    virtual std::string workspace_id()const=0;
    virtual std::int64_t workspace_revision()const=0;
    // Exact public identity from the immutable backend root budget. No route
    // destination or credential is carried or selected by the worker.
    virtual std::string provider_identity_json()const=0;
    virtual std::string execution_policy_id()const{return "native.direct-agent";}
    virtual std::optional<RootBudgetSpec> captured_budget()const{return {};}
    virtual std::optional<std::chrono::steady_clock::time_point> execution_deadline()const{return {};}
    virtual ModelRequest prepare_request(std::stop_token)=0;
    virtual ModelRequest rebuild_request(const std::string&,const ModelRequest&,std::stop_token){throw std::logic_error("Workspace context rebuild is not admitted");}
    virtual std::unique_ptr<AgentReadToolHost> read_host()=0;
};
}
