#pragma once
#include "agentflow/agent_process_tool_host.hpp"
namespace agentflow {
// Captured backend process service. Concrete command ownership and durable
// background-join observations stay with the coding context.
class AgentWorkerProcessService:public AgentProcessOwnerHost {
public:
    virtual bool join_processes(const std::string& attempt,std::stop_token={})=0;
};
}
