#pragma once
#include "agentflow/agent_callable_tool_host.hpp"
namespace agentflow {
class AgentWorkerCallableService:public AgentCallableToolHost {
public:
    virtual std::vector<std::string> tool_names(std::stop_token={})=0;
};
}
