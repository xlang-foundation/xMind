#pragma once
#include "agentflow/agent_runner_read_workspace.hpp"
#include "agentflow/agent_file_tool_host.hpp"
#include "agentflow/agent_process_tool_host.hpp"

namespace agentflow {
// Explicit additional admission for native file tools. Supplying this object
// through a read-only port does not enable file effects.
class AgentRunnerCodingWorkspace:public AgentRunnerReadWorkspace {
public:
    virtual std::unique_ptr<AgentFileToolHost> file_host()=0;
    virtual std::unique_ptr<AgentProcessOwnerHost> process_host(){return {};}
};
}
