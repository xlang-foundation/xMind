// Actual native approvals, guidance and xlang3 SQLite; the MCP client and
// model-call identities below are explicit fixtures, not live providers.
#include "agentflow/agent_remote_tool_host.hpp"
#include "agentflow/mcp_tool_registry.hpp"
#include "agentflow/repository_instruction_context.hpp"
#include "nlohmann/json.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <random>
#include <thread>
using namespace agentflow;using namespace std::chrono_literals;using Json=nlohmann::json;
namespace {
void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected remote-host rejection");}
struct Directory {
    std::filesystem::path parent=std::filesystem::canonical(std::filesystem::temp_directory_path()),path=parent/("xmind-remote-host-"+std::to_string(std::random_device{}()));
    Directory(){need(std::filesystem::create_directory(path),"Fixture directory must be fresh");}
    ~Directory(){std::error_code error;const auto resolved=std::filesystem::weakly_canonical(path,error);if(!error&&resolved.parent_path()==parent&&resolved.filename().string().starts_with("xmind-remote-host-"))std::filesystem::remove_all(path,error);}
};
void write(const std::filesystem::path& path,const std::string& value){std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<value;file.close();need(bool(file),"Fixture write failed");}
std::string read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);need(bool(file),"Actual file missing");return {std::istreambuf_iterator<char>(file),{}};}
class SyntheticMcpClient final:public McpToolClient {
public:
    std::atomic<int> sends{0};
    std::filesystem::path effect;
    McpServerDescription description{McpWireEra::legacy,"2025-06-18",R"({"tools":{}})",R"({"name":"synthetic remote-host fixture","version":"1"})",{}};
    explicit SyntheticMcpClient(std::filesystem::path file):effect(std::move(file)){}
    McpToolPage list_tools(std::optional<std::string> cursor,Deadline,std::stop_token)override{need(!cursor,"Fixture has one catalogue page");return {{{"record","Explicit synthetic fixture; writes one local effect file.",R"({"type":"object","properties":{"text":{"type":"string"}},"required":["text"],"additionalProperties":false})","{}",{}}},{}};}
    bool ready()const override{return true;}
    const McpServerDescription& server()const override{return description;}
    void shutdown()noexcept override{}
private:
    McpToolReply call_tool(const std::string& name,std::string_view arguments,Deadline,std::stop_token,const McpInputFulfillment&)override{
        need(name=="record","Registry must resolve the exact discovered peer tool");need(++sends==1,"No model-call identity may dispatch twice");write(effect,Json::parse(arguments).at("text").get<std::string>());
        return {"synthetic-reply",R"({"jsonrpc":"2.0","id":"synthetic-reply","result":{"content":[{"type":"text","text":"recorded"}]}})",R"({"content":[{"type":"text","text":"recorded"}]})",false,{}};
    }
};
struct Task {
    std::stop_source stop;std::future<AgentRemoteToolResult> result;
    Task(AgentRemoteToolHost& host,AgentRemoteToolInvocation call):result(std::async(std::launch::async,[this,&host,call=std::move(call)]{return host.invoke(call,stop.get_token());})){}
    ~Task(){stop.request_stop();if(result.valid())result.wait();}
};
Operation pending(PersistenceService& store,std::size_t previous){const auto deadline=std::chrono::steady_clock::now()+5s;while(std::chrono::steady_clock::now()<deadline){const auto operations=store.operations("run").get();if(operations.size()>previous)for(const auto& op:operations)if(op.state==OperationState::awaiting_approval)return op;std::this_thread::sleep_for(5ms);}throw std::runtime_error("Remote approval proposal deadline");}
}
int main(int argc,char** argv){if(argc!=3)return 2;try{
    {AgentRemoteToolInvocation call;call.run_id="run";call.attempt_id="model";call.call_id="unicode-\xe4\xb8\xad";call.workspace_id="workspace";call.workspace_revision=7;call.tool_name="mcp_fixture_record";call.tool_index=63;call.arguments_json=R"({"text":"literal \\n bytes"})";
        const auto wire=call.serialize();need(AgentRemoteToolInvocation::parse(wire).serialize()==wire,"Remote invocation must retain exact owned bytes");
        for(const auto* field:{"endpoint","credential","operation_id","permission","registry"}){auto forged=Json::parse(wire);forged[field]="worker-selected";rejects<std::invalid_argument>([&]{AgentRemoteToolInvocation::parse(forged.dump());});}
        for(const Json index:{Json(-1),Json(64),Json(1.5),Json("0"),Json(18446744073709551615ULL)}){auto invalid=Json::parse(wire);invalid["tool_index"]=index;rejects<std::invalid_argument>([&]{AgentRemoteToolInvocation::parse(invalid.dump());});}
        for(const Json revision:{Json(0),Json(9007199254740992LL),Json(1.5)}){auto invalid=Json::parse(wire);invalid["workspace_revision"]=revision;rejects<std::invalid_argument>([&]{AgentRemoteToolInvocation::parse(invalid.dump());});}
        rejects<std::invalid_argument>([&]{AgentRemoteToolInvocation::parse("{\"run_id\":\"first\","+wire.substr(1));});
        auto missing=Json::parse(wire);missing.erase("tool_name");rejects<std::invalid_argument>([&]{AgentRemoteToolInvocation::parse(missing.dump());});
        auto version=Json::parse(wire);version["protocol_version"]=2;rejects<std::invalid_argument>([&]{AgentRemoteToolInvocation::parse(version.dump());});
        std::cout<<"Remote invocation codec retained literal Unicode bytes and rejected worker-selected authority, duplicate fields and invalid numeric bounds."<<std::endl;
    }
    Directory directory;const auto root=directory.path/"workspace";std::filesystem::create_directory(root);write(root/"AGENTS.md","Initial fixture guidance.\n");
    WorkspaceTools workspace(root.string());const auto identity=workspace.identity();WorkspaceToolSet members(identity,7,{{identity,workspace.root_path()}});
    RepositoryInstructionContext guidance(workspace,workspace.repository_instructions("."));guidance.prepare();
    const auto database=(directory.path/"state.sqlite").string();std::string completed_operation,blocked_operation;
    {PersistenceService store(database,{argv[1],argv[2]});store.create_session("session","Explicit remote-host fixture").get();store.start_prompt_run("run","session",R"({"content":"remote-host fixture"})").get();store.transition("run",RunState::queued,RunState::running).get();
        SyntheticMcpClient peer(directory.path/"actual-peer-effect.txt");McpToolRegistry registry(peer,store,workspace,"synthetic-peer",1,std::chrono::steady_clock::now()+30s);const auto alias=registry.definitions().at(0).name;bool offered=true;
        AgentRemoteToolOwnerContext owner;owner.run_id="run";owner.primary_workspace=&workspace;owner.workspace_members=&members;owner.repository_context=&guidance;owner.deadline=std::chrono::steady_clock::now()+30s;
        owner.offered_tool=[&](const std::string& name){return offered&&name==alias;};owner.mcp_registry=[&](const std::string& name){return name==alias?&registry:nullptr;};owner.a2a_registry=[](const std::string&)->A2aAgentToolRegistry*{return nullptr;};
        auto host=make_agent_remote_tool_host(store,store,owner);
        AgentRemoteToolInvocation call;call.run_id="run";call.attempt_id="model-turn-1";call.call_id="call-\xe9\x9b\xaa";call.workspace_id=identity;call.workspace_revision=7;call.tool_name=alias;call.arguments_json=R"({"text":"once"})";
        auto invalid=call;invalid.protocol_version=2;rejects<std::invalid_argument>([&]{host->invoke(invalid);});
        invalid=call;invalid.run_id="foreign";rejects<ToolAccessDenied>([&]{host->invoke(invalid);});
        invalid=call;invalid.workspace_id="foreign";rejects<ToolAccessDenied>([&]{host->invoke(invalid);});
        invalid=call;invalid.workspace_revision=8;rejects<ToolAccessDenied>([&]{host->invoke(invalid);});
        invalid=call;invalid.tool_index=64;rejects<std::invalid_argument>([&]{host->invoke(invalid);});
        invalid=call;invalid.arguments_json=std::string(4*1024*1024+1,' ');rejects<std::invalid_argument>([&]{host->invoke(invalid);});
        invalid=call;invalid.arguments_json=R"({"text":"one","text":"two"})";rejects<std::invalid_argument>([&]{host->invoke(invalid);});
        offered=false;rejects<ToolAccessDenied>([&]{host->invoke(call);});offered=true;
        {std::stop_source stop;stop.request_stop();rejects<ToolCancelled>([&]{host->invoke(call,stop.get_token());});}
        {auto expired=owner;expired.deadline=std::chrono::steady_clock::now()-1s;auto expired_host=make_agent_remote_tool_host(store,store,expired);rejects<McpEffectNotDispatched>([&]{expired_host->invoke(call);});}
        auto denied=owner;denied.permission_layers={{{alias,"*","deny"}},{{"*","*","allow"}}};auto denied_host=make_agent_remote_tool_host(store,store,denied);rejects<AgentPermissionDenied>([&]{denied_host->invoke(call);});
        need(store.operations("run").get().empty()&&peer.sends==0,"Envelope, catalogue and policy rejection must precede proposals and dispatch");
        write(root/"AGENTS.md","Changed undelivered fixture guidance.\n");const auto missing=host->invoke(call);need(!missing.success&&Json::parse(missing.output_json)["error"]["code"]=="repository_instructions_required"&&store.operations("run").get().empty()&&peer.sends==0,"Undelivered guidance cannot propose remote effects");
        rejects<ToolAccessDenied>([&]{host->invoke(call);});guidance.prepare();call.attempt_id="model-turn-2";
        {Task task(*host,call);const auto proposal=pending(store,0);completed_operation=proposal.id;need(proposal.spec.tool=="mcp_tool"&&Json::parse(proposal.spec.arguments_json).contains("instructions"),"Owner must bind its actual root guidance to the proposal");
            auto duplicate=call;duplicate.arguments_json=R"({"text":"different"})";rejects<ToolAccessDenied>([&]{host->invoke(duplicate);});need(peer.sends==0&&!std::filesystem::exists(peer.effect),"Pending approval cannot dispatch");
            store.decide_operation(proposal.id,OperationDecision::allow,"fixture-controller").get();const auto result=task.result.get();need(result.success&&Json::parse(result.output_json)["acknowledged_by_peer"]==true&&peer.sends==1&&read(peer.effect)=="once","Exact approved call must dispatch and retain its actual receipt");}
        rejects<ToolAccessDenied>([&]{host->invoke(call);});call.attempt_id="model-turn-3";
        {Task task(*host,call);const auto proposal=pending(store,1);blocked_operation=proposal.id;write(root/"AGENTS.md","Changed while actual controller approval was pending.\n");store.decide_operation(proposal.id,OperationDecision::allow,"fixture-controller").get();rejects<ToolGuidanceChanged>([&]{(void)task.result.get();});need(peer.sends==1&&store.operation(proposal.id).get().state==OperationState::failed,"Stale approved guidance cannot dispatch a second peer effect");}
        offered=false;call.attempt_id="model-turn-4";rejects<ToolAccessDenied>([&]{host->invoke(call);});offered=true;
        store.transition("run",RunState::running,RunState::completed).get();rejects<ToolAccessDenied>([&]{host->invoke(call);});need(store.operations("run").get().size()==2&&peer.sends==1,"Retired owners and removed catalogue entries cannot add effects");store.close();
    }
    {PersistenceService store(database,{argv[1],argv[2]});need(store.operation(completed_operation).get().state==OperationState::succeeded&&store.operation(blocked_operation).get().state==OperationState::failed&&store.run("run").get().state==RunState::completed&&read(directory.path/"actual-peer-effect.txt")=="once","xlang3 SQLite reopen must retain both actual operation outcomes without replay");store.close();}
    std::cout<<"Remote-tool owner: actual approvals, inherited denial, admitted membership/catalogue, pending/completed replay rejection, root guidance delivery/change and xlang3 SQLite restart pass. MCP client/model identities are synthetic; no worker IPC, live provider or GUI acceptance.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
