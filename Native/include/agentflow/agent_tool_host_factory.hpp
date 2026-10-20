#pragma once
#include "agentflow/agent_file_tool_host.hpp"
#include "agentflow/agent_process_tool_host.hpp"
#include "agentflow/agent_read_tool_host.hpp"
#include "agentflow/agent_remote_tool_host.hpp"
#include "agentflow/agent_guidance_tool_host.hpp"
#include "agentflow/agent_callable_tool_host.hpp"

namespace agentflow {
// Trusted construction port, not a wire protocol. Contexts contain backend
// capabilities and callbacks and must never be deserialized from a worker.
// A worker adapter must instead return proxies bound to captured admission.
class AgentToolHostFactory {
public:
    virtual ~AgentToolHostFactory()=default;
    virtual std::unique_ptr<AgentFileToolHost> file(AgentFileToolOwnerContext)=0;
    virtual std::unique_ptr<AgentProcessOwnerHost> process(AgentProcessToolOwnerContext)=0;
    virtual std::unique_ptr<AgentReadToolHost> read(AgentReadToolOwnerContext)=0;
    virtual std::unique_ptr<AgentRemoteToolHost> remote(AgentRemoteToolOwnerContext)=0;
    virtual std::unique_ptr<AgentGuidanceToolHost> guidance(AgentGuidanceToolOwnerContext)=0;
    virtual std::unique_ptr<AgentCallableToolHost> callable(AgentCallableToolOwnerContext)=0;
};
std::shared_ptr<AgentToolHostFactory> make_persistence_agent_tool_host_factory(
    AgentRunJournal&,PersistenceService&);
}
