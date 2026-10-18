#pragma once
// Synthetic model/A2A peers; actual AgentRunner, approvals, files and SQLite.
#include "agentflow/agent_runner.hpp"
#include "agentflow/a2a_agent_registry.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>

namespace a2a_agent_guidance_test {
using Json=nlohmann::json;
using namespace std::chrono_literals;
inline void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
inline void write(const std::filesystem::path& file,const std::string& content){
    std::ofstream stream(file,std::ios::binary|std::ios::trunc);stream<<content;
    check(static_cast<bool>(stream),"Cannot write actual A2A guidance fixture");
}
struct Case {
    std::string name;
    std::filesystem::path root;
    std::atomic<int> model_calls=0,sends=0;
    std::atomic<bool> updated_guidance_seen=false;
    std::mutex mutex;
    std::string peer_error;
};
inline void run(const std::vector<std::string>& imports,const std::filesystem::path& parent){
    const std::string old_guidance="Initial local instruction: use the registered peer only after controller review.\n";
    const std::string new_guidance="Updated local instruction: reconsider remote delegation before proposing or sending it.\n";
    const std::string provider_secret="synthetic-a2a-guidance-model-key";
    std::array<Case,3> cases;cases[0].name="before";cases[1].name="approval";cases[2].name="fresh";
    httplib::Server peer;int port=0;
    for(auto& state:cases){
        state.root=parent/("agent-guidance-"+state.name);std::filesystem::create_directory(state.root);
        write(state.root/"AGENTS.md",old_guidance);
        auto* current=&state;
        peer.Get("/card/"+state.name,[&,current](const auto& request,auto& response){
            if(request.has_header("Authorization")){std::lock_guard lock(current->mutex);current->peer_error="Public A2A discovery received provider authentication";response.status=500;return;}
            response.set_header("A2A-Version","1.0");
            const auto endpoint="http://127.0.0.1:"+std::to_string(port)+"/remote/"+current->name;
            response.set_content(Json{{"name","Synthetic instruction-bound peer"},{"description","Actual loopback guidance fixture"},{"version","1.0"},
                {"capabilities",{{"streaming",false}}},{"supportedInterfaces",Json::array({{{"url",endpoint},{"protocolBinding","JSONRPC"},{"protocolVersion","1.0"}}})},{"skills",Json::array()}}.dump(),"application/json");
        });
        peer.Post("/remote/"+state.name,[current](const auto& request,auto& response){
            try{
                check(!request.has_header("Authorization"),"Public A2A send received provider authentication");
                const auto body=Json::parse(request.body);check(body.at("method")=="SendMessage","Expected exactly one non-streaming A2A send");
                check(body.at("params").at("message").at("parts").at(0).at("text")=="Delegate-"+current->name,"A2A send differs from the actual model call");
                ++current->sends;write(current->root/"peer-effect.txt","once");
                response.set_header("A2A-Version","1.0");
                response.set_content(Json{{"jsonrpc","2.0"},{"id",body.at("id")},{"result",{{"task",{{"id","remote-"+current->name},{"contextId","context-"+current->name},{"status",{{"state","TASK_STATE_COMPLETED"}}}}}}}}.dump(),"application/json");
            }catch(const std::exception& error){std::lock_guard lock(current->mutex);current->peer_error=error.what();response.status=500;}
        });
        peer.Post("/model/"+state.name,[&,current](const auto& request,auto& response){
            try{
                check(request.get_header_value("Authorization")=="Bearer "+provider_secret,"Model fixture must receive its own scoped credential");
                const auto body=Json::parse(request.body);const auto ordinal=++current->model_calls;
                std::string model_content;
                for(const auto& message:body.at("messages"))if(message.contains("content")&&message["content"].is_string())model_content+=message["content"].template get<std::string>();
                check(ordinal<=2,"Instruction protection must not retry the provider or remote effect");
                Json delta;std::string finish;
                if(ordinal==1){
                    check(model_content.find(Json(old_guidance).dump())!=std::string::npos,"First actual model request must contain the initial repository instruction");
                    std::string alias;
                    for(const auto& tool:body.at("tools")){const auto name=tool.at("function").at("name").template get<std::string>();if(name.starts_with("a2a_delegate_"))alias=name;}
                    check(!alias.empty(),"AgentRunner must expose its actual registered A2A alias");
                    if(current->name=="before")write(current->root/"AGENTS.md",new_guidance);
                    delta={{"tool_calls",Json::array({{{"index",0},{"id","delegate-"+current->name},{"type","function"},{"function",{{"name",alias},{"arguments",Json{{"task","Delegate-"+current->name}}.dump()}}}}})}};
                    finish="tool_calls";
                }else{
                    current->updated_guidance_seen=model_content.find(Json(new_guidance).dump())!=std::string::npos;
                    delta={{"content","Synthetic model observed the recorded native result"}};finish="stop";
                }
                response.set_content("data: "+Json{{"choices",Json::array({{{"index",0},{"delta",delta},{"finish_reason",finish}}})}}.dump()+"\n\ndata: [DONE]\n\n","text/event-stream");
            }catch(const std::exception& error){std::lock_guard lock(current->mutex);current->peer_error=error.what();response.status=500;}
        });
    }
    port=peer.bind_to_any_port("127.0.0.1");check(port>0,"Cannot bind actual A2A guidance peers");
    std::thread listener([&]{peer.listen_after_bind();});std::vector<std::string> failures;
    try{
        for(auto& state:cases){
            agentflow::PersistenceService store((state.root/"state.sqlite").string(),imports);
            agentflow::A2aRemoteAgentRegistry registrations(store);
            const auto origin="http://127.0.0.1:"+std::to_string(port);
            registrations.register_agent("research.guidance",origin+"/card/"+state.name,true,0);
            agentflow::SecretBytes secret({reinterpret_cast<const std::uint8_t*>(provider_secret.data()),provider_secret.size()});
            store.put_credential("guidance","model","provider:chat","Synthetic loopback provider",std::move(secret),0).get();
            agentflow::AgentSettings settings;settings.provider={origin+"/model/"+state.name,"synthetic-guidance-model",agentflow::Capability::supported,agentflow::Capability::supported};
            settings.workspace=state.root.string();settings.credential=agentflow::CredentialReference{"guidance","model","provider:chat"};settings.run_timeout=10s;
            agentflow::AgentRunner runner(store,settings);const auto id="a2a-guidance-"+state.name;
            store.create_session(id,"Actual A2A repository-guidance protection").get();runner.start(id,id,"Delegate the explicit synthetic task to the registered peer");
            std::stop_source cancel;auto execution=std::async(std::launch::async,[&]{return runner.execute(id,cancel.get_token());});
            bool changed=false;const auto deadline=std::chrono::steady_clock::now()+12s;
            while(execution.wait_for(0ms)!=std::future_status::ready){
                for(const auto& operation:store.operations(id).get())if(operation.state==agentflow::OperationState::awaiting_approval){
                    if(state.name=="approval"&&!changed){write(state.root/"AGENTS.md",new_guidance);changed=true;}
                    store.decide_operation(operation.id,agentflow::OperationDecision::allow,"synthetic-guidance-controller").get();
                }
                if(std::chrono::steady_clock::now()>=deadline)cancel.request_stop();
                std::this_thread::sleep_for(5ms);
            }
            const auto result=execution.get();const auto operations=store.operations(id).get();const auto history=store.history(id).get();
            Json diagnostic={{"case",state.name},{"run_state",agentflow::to_string(result.state)},{"model_calls",state.model_calls.load()},{"remote_sends",state.sends.load()},
                {"updated_guidance_seen",state.updated_guidance_seen.load()},{"peer_effect_exists",std::filesystem::exists(state.root/"peer-effect.txt")},{"operations",Json::array()}};
            for(const auto& operation:operations)diagnostic["operations"].push_back({{"state",agentflow::to_string(operation.state)},{"result_json",operation.result_json},{"has_instruction_binding",Json::parse(operation.spec.arguments_json).contains("instructions")}});
            std::cerr<<"[a2a-agent-guidance] "<<diagnostic.dump()<<'\n';
            try{
                {std::lock_guard lock(state.mutex);check(state.peer_error.empty(),state.peer_error.c_str());}
                check(result.state==agentflow::RunState::completed&&state.model_calls==2&&history.size()==4,"Actual AgentRunner must finish the bounded two-response conversation");
                const auto tool=Json::parse(Json::parse(history[2].json).at("content").template get<std::string>());
                if(state.name=="fresh"){
                    check(state.sends==1&&operations.size()==1&&operations[0].state==agentflow::OperationState::succeeded,"Unchanged approved A2A request must execute once");
                    check(Json::parse(operations[0].spec.arguments_json).contains("instructions"),"A2A proposal must carry the actual admitted repository-instruction binding");
                    check(std::filesystem::exists(state.root/"peer-effect.txt"),"Approved A2A peer must create its actual effect");
                }else{
                    check(state.sends==0&&!std::filesystem::exists(state.root/"peer-effect.txt"),"Changed instructions must block the actual remote send and effect");
                    check(tool.at("error").at("code")=="repository_instructions_required"&&state.updated_guidance_seen,"Changed instructions must return a recorded result and reach the next model request");
                    if(state.name=="before")check(operations.empty(),"Before-proposal instruction change must publish no approval operation");
                    else check(changed&&operations.size()==1&&operations[0].state==agentflow::OperationState::failed&&Json::parse(operations[0].result_json).at("reason")=="repository_instructions_changed_before_a2a_dispatch","Post-approval change must retire the exact operation before dispatch");
                }
            }catch(const std::exception& error){failures.push_back(state.name+": "+error.what());}
        }
    }catch(...){peer.stop();listener.join();throw;}
    peer.stop();listener.join();
    for(const auto& failure:failures)std::cerr<<"A2A guidance regression: "<<failure<<'\n';
    check(failures.empty(),"Actual AgentRunner A2A repository-guidance contract failed");
}
}
