// Actual native files, backend permissions and xlang3 SQLite. Run admission,
// controller decisions and model-call identities are explicit test inputs.
#include "agentflow/agent_file_tool_host.hpp"
#include "agentflow/patch_tool.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <random>
#include <thread>
using namespace agentflow;using namespace std::chrono_literals;using Json=nlohmann::json;
namespace {
void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected file-host rejection");}
struct Directory{
    std::filesystem::path parent=std::filesystem::canonical(std::filesystem::temp_directory_path()),path=parent/("xmind-file-host-"+std::to_string(std::random_device{}()));
    Directory(){need(std::filesystem::create_directory(path),"Owned fixture must be fresh");}
    ~Directory(){std::error_code error;const auto resolved=std::filesystem::weakly_canonical(path,error);if(!error&&resolved.parent_path()==parent&&resolved.filename().string().starts_with("xmind-file-host-"))std::filesystem::remove_all(path,error);}
};
void write(const std::filesystem::path& path,const std::string& text){std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<text;file.close();need(bool(file),"Fixture write failed");}
std::string read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);need(bool(file),"Actual file missing");return {std::istreambuf_iterator<char>(file),{}};}
struct Task{
    std::stop_source stop;std::future<AgentFileToolResult> result;
    Task(AgentFileToolHost& host,AgentFileToolInvocation call):result(std::async(std::launch::async,[this,&host,call=std::move(call)]{return host.invoke(call,stop.get_token());})){}
    ~Task(){stop.request_stop();if(result.valid())result.wait();}
};
Operation pending(PersistenceService& store,std::size_t previous){const auto deadline=std::chrono::steady_clock::now()+5s;while(std::chrono::steady_clock::now()<deadline){const auto operations=store.operations("run").get();if(operations.size()>previous){for(const auto& operation:operations)if(operation.state==OperationState::awaiting_approval)return operation;}std::this_thread::sleep_for(5ms);}throw std::runtime_error("Actual approval proposal deadline");}
}
int main(int argc,char** argv){if(argc!=3)return 2;try{
    Directory directory;const auto primary=directory.path/"primary",secondary=directory.path/"secondary";std::filesystem::create_directory(primary);std::filesystem::create_directory(secondary);
    WorkspaceTools workspace(primary.string()),attached(secondary.string());const auto workspace_id=workspace.identity(),attached_id=attached.identity();
    WorkspaceToolSet members(workspace_id,7,{{workspace_id,workspace.root_path()},{attached_id,attached.root_path()}});
    RepositoryInstructionContext root_context(workspace,workspace.repository_instructions(".")),attached_context(attached,attached.repository_instructions("."));root_context.prepare();attached_context.prepare();
    const auto database=(directory.path/"state.sqlite").string();std::string created_operation;
    {
        PersistenceService store(database,{argv[1],argv[2]});store.create_session("session","Actual file-host fixture").get();store.start_prompt_run("run","session",R"({"content":"file-host fixture"})").get();store.transition("run",RunState::queued,RunState::running).get();
        AgentFileToolOwnerContext owner;owner.run_id="run";owner.primary_workspace=&workspace;owner.workspace_members=&members;owner.repository_context=[&](std::size_t index){return index==0?&root_context:&attached_context;};owner.allowed_tools={"edit_file","create_file","apply_patch","restore_operation"};
        owner.permission_layers={{{"create_file","blocked.txt","deny"},{"apply_patch","blocked.txt","deny"}},{{"*","*","allow"}}};
        auto host=make_agent_file_tool_host(store,owner);
        const auto call=[&](const std::string& id,const std::string& tool,Json args){AgentFileToolInvocation value;value.run_id="run";value.attempt_id="model-turn-1";value.call_id=id;value.workspace_id=workspace_id;value.workspace_revision=7;value.folder_id=workspace_id;value.tool_name=tool;value.arguments_json=args.dump();return value;};
        auto creation=call("create-\xe9\x9b\xaa","create_file",{{"path","actual.txt"},{"content","created\n"}});
        auto invalid=creation;invalid.protocol_version=2;rejects<std::invalid_argument>([&]{host->invoke(invalid);});
        invalid=creation;invalid.run_id="foreign";rejects<ToolAccessDenied>([&]{host->invoke(invalid);});
        invalid=creation;invalid.workspace_revision=8;rejects<ToolAccessDenied>([&]{host->invoke(invalid);});
        invalid=creation;invalid.workspace_id=attached_id;rejects<ToolAccessDenied>([&]{host->invoke(invalid);});
        invalid=creation;invalid.folder_id=attached_id;rejects<ToolAccessDenied>([&]{host->invoke(invalid);});
        invalid=creation;invalid.arguments_json=Json{{"path","actual.txt"},{"content","bad"},{"folder_id",attached_id}}.dump();rejects<ToolAccessDenied>([&]{host->invoke(invalid);});
        invalid=creation;invalid.arguments_json=R"({"path":"one.txt","path":"two.txt","content":"bad"})";rejects<std::invalid_argument>([&]{host->invoke(invalid);});
        invalid=creation;invalid.arguments_json=std::string(4*1024*1024+1,' ');rejects<std::invalid_argument>([&]{host->invoke(invalid);});
        auto authority=call("foreign-authority","apply_patch",{{"patches",Json::array({Json{{"folder_id",workspace_id},{"patch_text","*** Begin Patch\n*** Add File: foreign.txt\n+must not write\n*** End Patch\n"}}})},{"root","C:/outside"}});rejects<std::invalid_argument>([&]{host->invoke(authority);});
        auto narrowed=owner;narrowed.allowed_tools={"edit_file"};auto edit_only=make_agent_file_tool_host(store,std::move(narrowed));rejects<ToolAccessDenied>([&]{edit_only->invoke(creation);});
        std::stop_source stopped;stopped.request_stop();rejects<ToolCancelled>([&]{host->invoke(creation,stopped.get_token());});
        need(store.operations("run").get().empty()&&!std::filesystem::exists(primary/"actual.txt")&&!std::filesystem::exists(secondary/"actual.txt"),"Invalid/foreign/cancelled requests cannot propose or write");
        rejects<AgentPermissionDenied>([&]{host->invoke(call("blocked","create_file",{{"path","blocked.txt"},{"content","denied"}}));});need(store.operations("run").get().empty(),"A later allow layer cannot erase inherited denial");
        rejects<AgentPermissionDenied>([&]{host->invoke(call("blocked-patch","apply_patch",{{"patch_text","*** Begin Patch\n*** Add File: blocked.txt\n+denied\n*** End Patch\n"}}));});need(store.operations("run").get().empty(),"Prepared patch paths must retain inherited denial before approval");
        {
            Task task(*host,creation);const auto proposal=pending(store,0);created_operation=proposal.id;need(!std::filesystem::exists(primary/"actual.txt"),"Actual creation requires approval");
            const auto bound=Json::parse(proposal.spec.arguments_json);need(bound["folder_id"]==workspace_id&&bound["workspace_revision"]==7,"Owner must bind its exact admitted folder/revision");
            rejects<ToolAccessDenied>([&]{host->invoke(creation);});need(store.operations("run").get().size()==1,"Duplicate pending call cannot produce a second proposal");
            store.decide_operation(proposal.id,OperationDecision::allow,"fixture-controller").get();need(task.result.get().success&&read(primary/"actual.txt")=="created\n","Approved owner dispatch must create actual bytes");
        }
        rejects<ToolAccessDenied>([&]{host->invoke(creation);});need(store.operations("run").get().size()==1,"Completed call cannot be replayed inside this owner");
        {
            auto edit=call("edit","edit_file",{{"path","actual.txt"},{"old_text","created"},{"new_text","edited"}});Task task(*host,edit);const auto proposal=pending(store,1);store.decide_operation(proposal.id,OperationDecision::allow,"fixture-controller").get();need(task.result.get().success&&read(primary/"actual.txt")=="edited\n","Shared owner must execute the real approved edit");
            auto restore=call("restore","restore_operation",{{"operation_id",proposal.id}});Task reverse(*host,restore);const auto inverse=pending(store,2);store.decide_operation(inverse.id,OperationDecision::allow,"fixture-controller").get();need(reverse.result.get().success&&read(primary/"actual.txt")=="created\n","Restore must use the approved durable before image");
        }
        const auto before_guidance=store.operations("run").get().size();write(primary/"AGENTS.md","Synthetic changed guidance must reach the next model context.\n");
        auto guided=call("guidance","create_file",{{"path","after-guide.txt"},{"content","fresh\n"}});const auto missing=host->invoke(guided);need(!missing.success&&Json::parse(missing.output_json)["error"]["code"]=="repository_instructions_required"&&store.operations("run").get().size()==before_guidance,"Undelivered guidance cannot create a proposal");
        need(root_context.prepare().find("Synthetic changed guidance")!=std::string::npos,"Owner must expose newly discovered guidance to the next model context");guided.attempt_id="model-turn-2";
        {Task task(*host,guided);const auto proposal=pending(store,before_guidance);store.decide_operation(proposal.id,OperationDecision::allow,"fixture-controller").get();need(task.result.get().success&&read(primary/"after-guide.txt")=="fresh\n","Fresh model attempt after guidance delivery can create an approved file");}
        const auto before_patch=store.operations("run").get().size();auto patch=call("multi-folder","apply_patch",{{"patches",Json::array({Json{{"folder_id",workspace_id},{"patch_text","*** Begin Patch\n*** Add File: multi-primary.txt\n+primary\n*** End Patch\n"}},Json{{"folder_id",attached_id},{"patch_text","*** Begin Patch\n*** Add File: multi-attached.txt\n+attached\n*** End Patch\n"}}})}});
        {Task task(*host,patch);const auto first=pending(store,before_patch);store.decide_operation(first.id,OperationDecision::allow,"fixture-controller").get();const auto second=pending(store,before_patch+1);store.decide_operation(second.id,OperationDecision::deny,"fixture-controller").get();const auto result=task.result.get();const auto report=Json::parse(result.output_json);need(!result.success&&report["state"]=="partial"&&report["succeeded_files"]==1&&read(primary/"multi-primary.txt")=="primary\n"&&!std::filesystem::exists(secondary/"multi-attached.txt"),"Cross-folder partial result must retain only the actual approved first effect");}
        store.transition("run",RunState::running,RunState::completed).get();rejects<ToolAccessDenied>([&]{host->invoke(call("retired","create_file",{{"path","retired.txt"},{"content","must not write"}}));});need(!std::filesystem::exists(primary/"retired.txt"),"A retired owner cannot dispatch more file work");store.close();
    }
    {PersistenceService store(database,{argv[1],argv[2]});need(store.operation(created_operation).get().state==OperationState::succeeded&&store.run("run").get().state==RunState::completed&&read(primary/"actual.txt")=="created\n","xlang3 SQLite restart must preserve the actual file outcome and retired owner");store.close();}
    std::cout<<"Native file-tool owner passed actual approved create/edit/restore, scoped membership and policy rejection, pending/completed call replay rejection, guidance delivery, cross-folder partial effects and xlang3 SQLite restart. No worker IPC/process placement, provider or GUI acceptance.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
