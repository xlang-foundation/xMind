// Actual workspace reads, controller approvals and xlang3-backed SQLite.
// No model/provider, fake filesystem adapter or worker-placement claim.
#include "agentflow/agent_read_tool_host.hpp"
#include "agentflow/persistence_service.hpp"
#include "agentflow/agent_permission_grants.hpp"
#include "agentflow/permission_waiter.hpp"
#include "agentflow/repository_instruction_context.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <random>
#include <thread>
using namespace agentflow;using namespace std::chrono_literals;using Json=nlohmann::json;
namespace {
void need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected workspace read rejection");}
struct Directory {
    std::filesystem::path parent=std::filesystem::canonical(std::filesystem::temp_directory_path()),path=parent/("xmind-read-host-"+std::to_string(std::random_device{}()));
    Directory(){need(std::filesystem::create_directory(path),"Fixture needs a fresh owned directory");}
    ~Directory(){std::error_code error;const auto resolved=std::filesystem::weakly_canonical(path,error);if(!error&&resolved.parent_path()==parent&&resolved.filename().string().starts_with("xmind-read-host-"))std::filesystem::remove_all(path,error);}
};
void write(const std::filesystem::path& path,const std::string& text){std::ofstream file(path,std::ios::binary);file<<text;file.close();need(bool(file),"Fixture write failed");}
struct Task {
    std::stop_source stop;std::future<AgentReadToolResult> result;
    Task(AgentReadToolHost& host,AgentWorkspaceInvocation call):result(std::async(std::launch::async,[this,&host,call=std::move(call)]{return host.invoke(call,stop.get_token());})){}
    ~Task(){stop.request_stop();if(result.valid())result.wait();}
};
Operation pending(PersistenceService& store){const auto until=std::chrono::steady_clock::now()+5s;while(std::chrono::steady_clock::now()<until){for(const auto& op:store.operations("run").get())if(op.state==OperationState::awaiting_approval)return op;std::this_thread::sleep_for(5ms);}throw std::runtime_error("Read approval not observed");}
}
int main(int argc,char** argv){if(argc!=3)return 2;try{
    Directory directory;const auto primary=directory.path/"primary",secondary=directory.path/"attached";
    std::filesystem::create_directory(primary);std::filesystem::create_directory(secondary);std::filesystem::create_directory(primary/"private");
    write(primary/"same.txt","primary bytes\n");write(secondary/"same.txt","attached bytes\n");write(primary/"secret.txt","denied secret\n");write(primary/"private"/"hidden.txt","restricted listing\n");write(secondary/"guidance.txt","not disclosed under stale guidance\n");
    WorkspaceTools workspace(primary.string()),attached(secondary.string());const auto workspace_id=workspace.identity(),attached_id=attached.identity();
    WorkspaceToolSet members(workspace_id,7,{{workspace_id,workspace.root_path()},{attached_id,attached.root_path()}});
    RepositoryInstructionContext root_context(workspace,workspace.repository_instructions(".")),attached_context(attached,attached.repository_instructions("."));root_context.prepare();attached_context.prepare();
    const auto database=(directory.path/"state.sqlite").string();std::string approved_id,withheld_id;
    {
        PersistenceService store(database,{argv[1],argv[2]});store.create_session("session","Actual workspace read owner").get();store.start_prompt_run("run","session",R"({"content":"bound reads"})").get();store.transition("run",RunState::queued,RunState::running).get();
        AgentReadToolOwnerContext owner;owner.run_id="run";owner.primary_workspace=&workspace;owner.workspace_members=&members;owner.repository_context=[&](std::size_t index){return index==0?&root_context:&attached_context;};owner.offered_tool=is_agent_read_tool;owner.deadline=std::chrono::steady_clock::now()+30s;
        const auto call=[&](const std::string& id,const std::string& folder,const std::string& tool,Json args){AgentWorkspaceInvocation value;value.run_id="run";value.attempt_id="attempt-1";value.call_id=id;value.workspace_id=workspace_id;value.workspace_revision=7;value.folder_id=folder;value.tool_name=tool;value.arguments_json=args.dump();return value;};
        auto host=make_agent_read_tool_host(store,store,owner);const auto original=call("actual-primary",workspace_id,"read_file",{{"path","same.txt"}});
        auto invalid=original;invalid.protocol_version=2;rejects<std::invalid_argument>([&]{host->invoke(invalid);});invalid=original;invalid.run_id="foreign";rejects<ToolAccessDenied>([&]{host->invoke(invalid);});invalid=original;invalid.workspace_revision=8;rejects<ToolAccessDenied>([&]{host->invoke(invalid);});invalid=original;invalid.folder_id="foreign";rejects<ToolAccessDenied>([&]{host->invoke(invalid);});invalid=original;invalid.folder_id=attached_id;rejects<ToolAccessDenied>([&]{host->invoke(invalid);});invalid=original;invalid.arguments_json=R"({"path":"same.txt","path":"secret.txt"})";rejects<std::invalid_argument>([&]{host->invoke(invalid);});
        const auto first=host->invoke(original);need(first.success&&Json::parse(first.output_json)["content"]=="primary bytes\n","Owner must read actual primary bytes");rejects<ToolAccessDenied>([&]{host->invoke(original);});
        const auto actual_attached=host->invoke(call("actual-attached",attached_id,"read_file",{{"path","same.txt"},{"folder_id",attached_id}}));need(actual_attached.success&&Json::parse(actual_attached.output_json)["content"]=="attached bytes\n","Attached read must not fall back to the primary folder");need(store.operations("run").get().empty(),"Unasked reads and rejected envelopes must not create approvals");
        owner.inherited_permission_layers={{{"read_file","secret.txt","deny"},{"*","private/**","deny"}}};owner.permission_rules={{"read_file","*","ask"},{"read_file","secret.txt","allow"}};
        auto asked=make_agent_read_tool_host(store,store,owner);rejects<AgentPermissionDenied>([&]{asked->invoke(call("denied",workspace_id,"read_file",{{"path","secret.txt"}}));});rejects<AgentPermissionDenied>([&]{asked->invoke(call("restricted-directory",workspace_id,"list_files",{{"path","."}}));});need(store.operations("run").get().empty(),"Inherited denial must precede approval and data dispatch");
        auto read_attached=call("asked-attached",attached_id,"read_file",{{"path","same.txt"},{"folder_id",attached_id}});
        {Task task(*asked,read_attached);const auto op=pending(store);approved_id=op.id;need(op.spec.workspace==attached_id&&op.spec.arguments_json==read_attached.arguments_json,"Approval must bind the actual folder and arguments");rejects<ToolAccessDenied>([&]{asked->invoke(read_attached);});AgentPermissionGrantStore grants(store);const auto request=grants.request(op.id);need(request&&request->workspace_id==attached_id&&request->policy_binding==agent_permission_policy_binding(owner.inherited_permission_layers,owner.permission_rules),"Backend must bind saved grant to original layered policy");grants.grant("session",op.id);store.decide_operation(op.id,OperationDecision::allow,"fixture-controller").get();const auto value=task.result.get();need(value.success&&Json::parse(value.output_json)["content"]=="attached bytes\n","Approved read must return actual attached bytes");}
        auto reused=read_attached;reused.call_id="reuse-attached-grant";need(asked->invoke(reused).success,"Exact attached grant must be reusable");need(store.operations("run").get().size()==2,"Reuse must retain a distinct durable approved read receipt");
        {Task task(*asked,call("primary-needs-approval",workspace_id,"read_file",{{"path","same.txt"}}));const auto op=pending(store);need(op.spec.workspace==workspace_id,"Attached grant must not authorize the same path in primary");store.decide_operation(op.id,OperationDecision::deny,"fixture-controller").get();rejects<PermissionDenied>([&]{task.result.get();});}
        {Task task(*asked,call("guidance-changes-while-waiting",attached_id,"read_file",{{"path","guidance.txt"},{"folder_id",attached_id}}));const auto op=pending(store);withheld_id=op.id;write(secondary/"AGENTS.md","Changed attached guidance must reach the next model request.\n");store.decide_operation(op.id,OperationDecision::allow,"fixture-controller").get();const auto result=task.result.get();need(!result.success&&Json::parse(result.output_json)["error"]["code"]=="repository_instructions_required"&&result.output_json.find("not disclosed")==std::string::npos,"Changed guidance during approval must withhold file bytes");need(store.operation(op.id).get().state==OperationState::failed,"Claimed stale-guidance read must retire as failed");}
        std::stop_source stopped;stopped.request_stop();rejects<ToolCancelled>([&]{asked->invoke(call("cancelled",workspace_id,"read_file",{{"path","same.txt"}}),stopped.get_token());});
        auto hidden_owner=owner;hidden_owner.offered_tool=[](const std::string&){return false;};auto hidden=make_agent_read_tool_host(store,store,hidden_owner);rejects<ToolAccessDenied>([&]{hidden->invoke(call("not-offered",workspace_id,"read_file",{{"path","same.txt"}}));});need(store.operations("run").get().size()==4,"Rejected calls must not create extra operations");store.transition("run",RunState::running,RunState::completed).get();rejects<ToolAccessDenied>([&]{asked->invoke(call("retired",workspace_id,"read_file",{{"path","same.txt"}}));});
    }
    {PersistenceService reopened(database,{argv[1],argv[2]});need(reopened.operation(approved_id).get().state==OperationState::succeeded&&reopened.operation(withheld_id).get().state==OperationState::failed&&reopened.operations("run").get().size()==4,"Read receipts must survive actual xlang3 SQLite reopen without replay");const auto grants=AgentPermissionGrantStore(reopened).list("session");need(grants.grants.size()==1&&grants.grants.front().workspace_id==attached_id,"Only the selected attached-folder grant may persist");}
    std::cout<<"workspace read owner contract passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
