#include "agentflow/agent_definitions.hpp"
#include "agentflow/agent_worker_read_executor.hpp"
#include "agentflow/execution_platform.hpp"
#include "agentflow/http_stream_transport.hpp"
#include "agentflow/owner_process.hpp"
#include "agentflow/persistence_service.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <thread>
#define NOMINMAX
#include <windows.h>

using namespace agentflow;
using namespace std::chrono_literals;
using Json=nlohmann::json;
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
class ExpiredProvider final:public AgentModelGateway {
public:
    std::atomic<unsigned> calls=0;
    ModelCompletion complete(const ChatProviderConfig&,const ModelRequest&,
        ChatCompletionStream::Sink,std::stop_token)override{
        ++calls;
        throw TransportTimeout("Synthetic provider request deadline; private transport detail");
    }
    ResponsesTokenCount count_context(const ChatProviderConfig&,const ModelRequest&,std::stop_token)override{
        throw std::logic_error("Context counting is outside this fixture");
    }
    ResponsesCompactionResult compact_context(const ChatProviderConfig&,const ModelRequest&,std::stop_token)override{
        throw std::logic_error("Context compaction is outside this fixture");
    }
};
}
int main(int argc,char** argv){
    if(argc!=4)return 2;
    const auto parent=std::filesystem::canonical(std::filesystem::current_path());
    const auto directory=parent/("model-deadline-"+std::to_string(GetCurrentProcessId()));
    try{
        require(std::filesystem::create_directory(directory),"Deadline fixture must be fresh");
        const auto workspace=directory/"workspace";
        std::filesystem::create_directory(workspace);
        {
            PersistenceService store((directory/"state.sqlite").string(),{argv[1],argv[2]});
            const auto gateway=std::make_shared<ExpiredProvider>();
            AgentSettings settings;
            settings.provider={"https://fixture.invalid/v1/chat/completions","deadline-fixture",Capability::supported,Capability::supported};
            settings.workspace=workspace.string();settings.approved_edits=true;
            settings.a2a_enabled=false;settings.run_timeout=20s;
            AgentDefinitionStore(store).apply("agents:\n  - id: deadline.fixture\n    mode: primary\n    placement: process\n    model_id: deadline-fixture\n    instructions: Observe the provider outcome.\n    tools: []\n");
            const auto image=ForegroundProcess::bind_executable(argv[3]);
            store.create_session("deadline-session","Provider deadline before root budget expires").get();
            {
                ExecutionPlatform service(store,settings,1,2,[&](PersistenceService& actual,const AgentSettings& captured){
                    return make_agent_worker_coding_executor(actual,captured,image,gateway);
                });
                service.select_session_agent("deadline-session",service.agent_definitions().front(),0);
                service.submit("deadline-root","deadline-session","Observe this actual model request outcome.");
                const auto until=std::chrono::steady_clock::now()+10s;
                while(!service.idle()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(5ms);
                require(service.idle()&&service.healthy(),"Provider deadline must physically drain its worker");
                require(store.run("deadline-root").get().state==RunState::failed,"Provider deadline must fail the run");
            }
            const auto events=store.events("deadline-root").get();
            const auto admitted=std::find_if(events.begin(),events.end(),[](const auto& e){return e.kind=="worker.lease.admitted";});
            require(admitted!=events.end(),"Regression must use a real SDK-connected worker");
            const auto generation=Json::parse(admitted->json).at("generation").get<std::string>();
            const auto lease=store.agent_worker_lease(generation).get();
            require(lease.state=="retired"&&observe_owner_exit(lease.spec.pid,lease.spec.creation_time,0).exited,"Original worker must exit before terminal acceptance");
            require(gateway->calls==1&&store.root_budget("deadline-root").get().model_calls_reserved==1,"A deadline must not replay or refund its dispatched model call");
            require(store.run_history("deadline-root").get().size()==1&&store.operations("deadline-root").get().empty(),"A deadline must not fabricate an assistant answer or tool effect");
            const auto cause=std::find_if(events.begin(),events.end(),[](const auto& e){return e.kind=="worker.model.uncertain";});
            require(cause!=events.end()&&Json::parse(cause->json).at("generation")==generation&&Json::parse(cause->json).at("failure_code")=="deadline","Backend must retain the actual model deadline fact");
            const auto failed=std::find_if(events.begin(),events.end(),[](const auto& e){return e.kind=="run.failed";});
            require(failed!=events.end()&&Json::parse(failed->json).at("reason")=="deadline","Model deadline must survive supervision while the root still has time");
            require(std::count_if(events.begin(),events.end(),[](const auto& e){return e.kind=="run.failed";})==1,"Deadline must terminalize once");
            store.close();
        }
        require(std::filesystem::canonical(directory).parent_path()==parent&&directory.filename().string().starts_with("model-deadline-"),"Fixture cleanup escaped its build directory");
        std::filesystem::remove_all(directory);
        std::cout<<"Actual provider deadline preserved through native worker retirement, with one model dispatch and no effects or fabricated response.\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"; retained fixture: "<<directory<<'\n';return 1;}
}
