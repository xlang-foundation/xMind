#include "agentflow/agent_tool_host_factory.hpp"

namespace agentflow {
namespace {
class PersistenceAgentToolHostFactory final:public AgentToolHostFactory {
public:
    PersistenceAgentToolHostFactory(AgentRunJournal& journal,PersistenceService& store):journal_(journal),store_(store){}
    std::unique_ptr<AgentFileToolHost> file(AgentFileToolOwnerContext owner)override{return make_agent_file_tool_host(store_,std::move(owner));}
    std::unique_ptr<AgentProcessOwnerHost> process(AgentProcessToolOwnerContext owner)override{return make_agent_process_tool_host(store_,std::move(owner));}
    std::unique_ptr<AgentReadToolHost> read(AgentReadToolOwnerContext owner)override{return make_agent_read_tool_host(journal_,store_,std::move(owner));}
    std::unique_ptr<AgentRemoteToolHost> remote(AgentRemoteToolOwnerContext owner)override{return make_agent_remote_tool_host(store_,journal_,std::move(owner));}
    std::unique_ptr<AgentGuidanceToolHost> guidance(AgentGuidanceToolOwnerContext owner)override{return make_agent_guidance_tool_host(journal_,std::move(owner));}
    std::unique_ptr<AgentCallableToolHost> callable(AgentCallableToolOwnerContext owner)override{return make_agent_callable_tool_host(journal_,std::move(owner));}
private:
    AgentRunJournal& journal_;
    PersistenceService& store_;
};
}
std::shared_ptr<AgentToolHostFactory> make_persistence_agent_tool_host_factory(AgentRunJournal& journal,PersistenceService& store){
    return std::make_shared<PersistenceAgentToolHostFactory>(journal,store);
}
}
