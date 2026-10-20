#include "agentflow/agent_backend_runtime.hpp"
#include "agentflow/mcp_client_factory.hpp"
#include "agentflow/mcp_tool_registry.hpp"
#include "agentflow/mcp_run_subscription_observer.hpp"
#include "agentflow/a2a_tool_registry.hpp"

namespace agentflow {
namespace {
class PersistenceMcpPeer final:public AgentMcpPeer {
public:
    PersistenceMcpPeer(PersistenceService& store,McpServerSetting setting,McpDeadline deadline,std::stop_token cancel)
        :store_(store),setting_(std::move(setting)),deadline_(deadline),cancel_(cancel){
        client_=connect_mcp_client(setting_,store_,deadline_,cancel_,&credentials_);
    }
    ~PersistenceMcpPeer()override{stop_observer();}
    std::string protocol_version()const override{return client_->server().protocol_version;}
    void discover(WorkspaceTools& workspace,std::vector<WorkspaceToolFolder> roots)override{
        if(registry_)throw std::logic_error("MCP peer catalogue already discovered");
        registry_=std::make_unique<McpToolRegistry>(*client_,store_,workspace,setting_.id,setting_.revision,deadline_,cancel_,&credentials_,invalidated_,std::move(roots));
    }
    std::vector<ModelToolDefinition> definitions()const override{return registry().definitions();}
    std::string approval_bindings_json()const override{return registry().approval_bindings_json();}
    McpToolRegistry* owner_registry()noexcept override{return registry_.get();}
    bool start_observer(const std::string& run)override{
        if(observer_)throw std::logic_error("MCP peer observer already started");
        const auto filter=mcp_run_subscription_filter(client_->server());if(!filter)return true;
        try{
            auto client=connect_mcp_client(setting_,store_,deadline_,cancel_);
            auto observer=std::make_unique<McpRunSubscriptionObserver>(store_,run,setting_.id,setting_.revision,std::move(client),*filter,deadline_,invalidated_);
            observer->start();observer_=std::move(observer);return true;
        }catch(const std::exception&){invalidated_->store(true,std::memory_order_release);return false;}
    }
    void stop_observer()noexcept override{if(observer_){observer_->stop();observer_.reset();}}
private:
    const McpToolRegistry& registry()const{if(!registry_)throw std::logic_error("MCP peer catalogue is unavailable");return *registry_;}
    PersistenceService& store_;
    McpServerSetting setting_;
    McpDeadline deadline_;
    std::stop_token cancel_;
    std::vector<SecretBytes> credentials_;
    std::unique_ptr<McpToolClient> client_;
    std::shared_ptr<std::atomic<bool>> invalidated_=std::make_shared<std::atomic<bool>>(false);
    std::unique_ptr<McpToolRegistry> registry_;
    std::unique_ptr<McpRunSubscriptionObserver> observer_;
};
class PersistenceAgentBackendRuntime final:public AgentBackendRuntime {
public:
    explicit PersistenceAgentBackendRuntime(PersistenceService& store):store_(store),a2a_(store){}
    std::unique_ptr<AgentProcessToolHost> process(WorkspaceTools& workspace,const std::string& root,std::vector<ProcessProfile> profiles)override{
        return std::make_unique<ProcessExecutor>(store_,workspace,root,std::move(profiles));
    }
    std::shared_ptr<AgentMcpPeer> connect_mcp(const McpServerSetting& server,std::chrono::steady_clock::time_point deadline,std::stop_token cancel)override{
        return std::make_shared<PersistenceMcpPeer>(store_,server,deadline,cancel);
    }
    A2aRemoteAgentCatalog a2a_catalog()override{return a2a_.list();}
    std::unique_ptr<A2aAgentToolRegistry> discover_a2a(WorkspaceTools& workspace,std::chrono::milliseconds timeout,std::stop_token cancel)override{
        return std::make_unique<A2aAgentToolRegistry>(a2a_,store_,workspace,timeout,cancel);
    }
private:
    PersistenceService& store_;
    A2aRemoteAgentRegistry a2a_;
};
}
std::shared_ptr<AgentBackendRuntime> make_persistence_agent_backend_runtime(PersistenceService& store){return std::make_shared<PersistenceAgentBackendRuntime>(store);}
}
