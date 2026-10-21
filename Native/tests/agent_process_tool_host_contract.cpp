// Real xlang3 SQLite, approvals and one actual xlang3 process. Recording-host
// cases are explicit owner-boundary probes, not evidence of worker execution.
#include "agentflow/agent_process_tool_host.hpp"
#include "agentflow/process_executor.hpp"
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
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected process-host rejection");}
struct Directory{
    std::filesystem::path parent=std::filesystem::canonical(std::filesystem::temp_directory_path()),path=parent/("xmind-process-host-"+std::to_string(std::random_device{}()));
    Directory(){need(std::filesystem::create_directory(path),"Fixture must own a fresh directory");}
    ~Directory(){std::error_code error;const auto resolved=std::filesystem::weakly_canonical(path,error);if(!error&&resolved.parent_path()==parent&&resolved.filename().string().starts_with("xmind-process-host-"))std::filesystem::remove_all(path,error);}
};
void write(const std::filesystem::path& path,const std::string& text){std::ofstream file(path,std::ios::binary);file<<text;file.close();need(bool(file),"Fixture write failed");}
std::string read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);need(bool(file),"Expected actual process output file");return {std::istreambuf_iterator<char>(file),{}};}
struct RecordingProcess final:AgentProcessToolHost {
    std::vector<AgentProcessInvocation> calls;std::vector<ProcessProfile> bindings;
    ModelToolDefinition definition()const override{return {"run_process","probe",R"({"type":"object"})"};}
    ModelToolDefinition wait_definition()const override{return {"wait_process","probe",R"({"type":"object"})"};}
    ModelToolDefinition stop_definition()const override{return {"stop_process","probe",R"({"type":"object"})"};}
    const std::vector<ProcessProfile>& profiles()const noexcept override{return bindings;}
    std::string invoke(const AgentProcessInvocation& call,std::stop_token,InstructionPrecondition guidance)override{need(bool(guidance.verify),"Owner must supply its own guidance verifier");calls.push_back(call);return R"({"state":"succeeded"})";}
    std::string wait(const std::string&,const std::string&,std::stop_token)override{return R"({"state":"uncertain"})";}
    std::string stop(const std::string&,const std::string&,std::stop_token)override{return R"({"state":"stopping"})";}
    std::vector<std::string> join_run(const std::string&,std::stop_token)override{return {};}
};
struct Task{
    std::stop_source stop;std::future<AgentProcessToolResult> result;
    Task(AgentProcessOwnerHost& host,AgentProcessToolInvocation call):result(std::async(std::launch::async,[this,&host,call=std::move(call)]{return host.invoke(call,stop.get_token());})){}
    ~Task(){stop.request_stop();if(result.valid())result.wait();}
};
Operation pending(PersistenceService& store){const auto deadline=std::chrono::steady_clock::now()+5s;while(std::chrono::steady_clock::now()<deadline){for(const auto& operation:store.operations("run").get())if(operation.state==OperationState::awaiting_approval)return operation;std::this_thread::sleep_for(5ms);}throw std::runtime_error("Actual process proposal deadline");}
}
int main(int argc,char** argv){if(argc!=4)return 2;try{
    Directory directory;const auto primary=directory.path/"primary",secondary=directory.path/"secondary";std::filesystem::create_directory(primary);std::filesystem::create_directory(secondary);
    WorkspaceTools workspace(primary.string()),attached(secondary.string());const auto workspace_id=workspace.identity(),attached_id=attached.identity();
    WorkspaceToolSet members(workspace_id,7,{{workspace_id,workspace.root_path()},{attached_id,attached.root_path()}});
    RepositoryInstructionContext root_context(workspace,workspace.repository_instructions(".")),attached_context(attached,attached.repository_instructions("."));root_context.prepare();attached_context.prepare();
    const auto database=(directory.path/"state.sqlite").string();std::string completed_operation;
    {
        PersistenceService store(database,{argv[1],argv[2]});store.create_session("session","Native process owner fixture").get();store.start_prompt_run("run","session",R"({"content":"actual owner dispatch"})").get();store.transition("run",RunState::queued,RunState::running).get();
        store.append_event("run","agent.workspace_membership",Json{{"workspace_id",workspace_id},{"revision",7},{"primary_folder_id",workspace_id},{"snapshot","run_admission"},{"folders",Json::array({Json{{"folder_id",workspace_id},{"root",workspace.root_path()}},Json{{"folder_id",attached_id},{"root",attached.root_path()}}})}}.dump()).get();
        RecordingProcess recording;AgentProcessToolOwnerContext owner;owner.run_id="run";owner.process=&recording;owner.primary_workspace=&workspace;owner.workspace_members=&members;owner.repository_context=[&](std::size_t index){return index==0?&root_context:&attached_context;};owner.offered_tool=is_agent_process_tool;owner.deadline=std::chrono::steady_clock::now()+30s;owner.allow_background=false;
        auto host=make_agent_process_tool_host(store,owner);
        const auto call=[&](const std::string& id,const std::string& tool,Json args){AgentProcessToolInvocation value;value.run_id="run";value.attempt_id="attempt-1";value.call_id=id;value.workspace_id=workspace_id;value.workspace_revision=7;value.folder_id=workspace_id;value.tool_name=tool;value.arguments_json=args.dump();return value;};
        auto invocation=call("primary-\xe9\x9b\xaa","run_process",{{"profile","xlang3"},{"arguments",Json::array()},{"workdir","."}});
        const auto wire=invocation.serialize();need(AgentProcessToolInvocation::parse(wire).serialize()==wire,"Process wire must preserve exact scoped values and UTF-8 call IDs");
        for(const auto* tool:{"run_process","wait_process","stop_process"}){auto value=invocation;value.tool_name=tool;need(AgentProcessToolInvocation::parse(value.serialize()).tool_name==tool,"All admitted process tools must round trip");}
        for(const auto* field:{"operation_id","approval_id","executable","root","owner_policy","expires_unix_ms"}){auto value=Json::parse(wire);value[field]="forged";rejects<std::invalid_argument>([&]{AgentProcessToolInvocation::parse(value.dump());});}
        for(const auto* field:{"protocol_version","run_id","attempt_id","call_id","workspace_id","folder_id","workspace_revision","tool_name","arguments_json"}){auto missing=Json::parse(wire);missing.erase(field);rejects<std::invalid_argument>([&]{AgentProcessToolInvocation::parse(missing.dump());});auto nested=Json::parse(wire);nested[field]=Json::object();rejects<std::invalid_argument>([&]{AgentProcessToolInvocation::parse(nested.dump());});}
        rejects<std::invalid_argument>([&]{AgentProcessToolInvocation::parse(wire.substr(0,wire.size()-1)+",\"call_id\":\"forged\"}");});
        for(const auto revision:{Json(0),Json(-1),Json(1.5),Json(9007199254740992ULL)}){auto value=Json::parse(wire);value["workspace_revision"]=revision;rejects<std::invalid_argument>([&]{AgentProcessToolInvocation::parse(value.dump());});}
        {auto value=invocation;value.tool_name="create_file";rejects<std::invalid_argument>([&]{value.serialize();});value=invocation;value.call_id.push_back('\0');rejects<std::invalid_argument>([&]{value.serialize();});}
        auto invalid=invocation;invalid.protocol_version=2;rejects<std::invalid_argument>([&]{host->invoke(invalid);});
        invalid=invocation;invalid.run_id="foreign";rejects<ToolAccessDenied>([&]{host->invoke(invalid);});
        invalid=invocation;invalid.workspace_id=attached_id;rejects<ToolAccessDenied>([&]{host->invoke(invalid);});
        invalid=invocation;invalid.workspace_revision=8;rejects<ToolAccessDenied>([&]{host->invoke(invalid);});
        invalid=invocation;invalid.folder_id=attached_id;rejects<ToolAccessDenied>([&]{host->invoke(invalid);});
        invalid=invocation;invalid.folder_id="foreign-folder";rejects<ToolAccessDenied>([&]{host->invoke(invalid);});
        invalid=invocation;invalid.arguments_json=R"({"workdir":".","workdir":"foreign"})";rejects<std::invalid_argument>([&]{host->invoke(invalid);});
        invalid=invocation;invalid.arguments_json=std::string(4*1024*1024+1,' ');rejects<std::invalid_argument>([&]{host->invoke(invalid);});
        auto narrowed=owner;narrowed.offered_tool=[](const auto& name){return name=="wait_process";};auto wait_only=make_agent_process_tool_host(store,narrowed);rejects<ToolAccessDenied>([&]{wait_only->invoke(invocation);});
        auto denied=owner;denied.permission_layers={{{"run_process","*","deny"}},{{"*","*","allow"}}};auto denied_host=make_agent_process_tool_host(store,denied);rejects<AgentPermissionDenied>([&]{denied_host->invoke(invocation);});
        auto expired=owner;expired.deadline=std::chrono::steady_clock::now()-1ms;auto expired_host=make_agent_process_tool_host(store,expired);rejects<ProcessBeforeDispatchError>([&]{expired_host->invoke(invocation);});
        std::stop_source cancelled;cancelled.request_stop();rejects<ToolCancelled>([&]{host->invoke(invocation,cancelled.get_token());});
        need(recording.calls.empty()&&store.operations("run").get().empty(),"Invalid/denied/obsolete calls must not reach process preparation or approval");
        need(host->invoke(invocation).success,"Bound owner may dispatch once");need(recording.calls.size()==1,"Exactly one low-level dispatch expected");const auto& bound=recording.calls.front();need(bound.run_id=="run"&&bound.workspace_id==workspace_id&&bound.workspace_revision==7&&bound.folder_id==workspace_id&&!bound.allow_background&&bound.operation_id.size()==32&&bound.expires_unix_ms>0,"Backend must choose operation identity, expiry and background policy");
        rejects<ToolAccessDenied>([&]{host->invoke(invocation);});need(recording.calls.size()==1,"Duplicate call must not replay a process");
        need(!host->invoke(call("wait-uncertain","wait_process",{{"job_id","observed"}})).success,"Uncertain wait must remain unsuccessful");need(!host->invoke(call("stop-pending","stop_process",{{"job_id","observed"}})).success,"A stop request is not a confirmed completed stop");
        write(secondary/"AGENTS.md","New attached-folder instructions\n");auto guided=call("guided","run_process",{{"profile","xlang3"},{"arguments",Json::array()},{"folder_id",attached_id},{"workdir","."}});guided.folder_id=attached_id;
        const auto withheld=host->invoke(guided);need(!withheld.success&&Json::parse(withheld.output_json)["error"]["code"]=="repository_instructions_required"&&recording.calls.size()==1,"Undelivered folder guidance must withhold dispatch and approval");attached_context.prepare();
        ProcessProfile profile{"xlang3",argv[3],1,{},10s};profile.xlang3_library=argv[2];ProcessExecutor executor(store,workspace,workspace.root_path(),{profile});auto actual_owner=owner;actual_owner.process=&executor;actual_owner.allow_background=false;actual_owner.deadline=std::chrono::steady_clock::now()+30s;auto actual=make_agent_process_tool_host(store,actual_owner);
        auto command=call("actual-process","run_process",{{"profile","xlang3"},{"arguments",Json::array({"-B","-c","import sys; assert sys.implementation.name == 'xlang3'; from pathlib import Path; Path('owner-marker.txt').write_bytes(b'actual attached process\\n'); print('owner-process-ok')"})},{"folder_id",attached_id},{"workdir","."},{"timeout_ms",10000}});command.folder_id=attached_id;
        {Task task(*actual,command);const auto proposal=pending(store);completed_operation=proposal.id;const auto args=Json::parse(proposal.spec.arguments_json);need(args["folder_id"]==attached_id&&args["workspace_revision"]==7&&!std::filesystem::exists(secondary/"owner-marker.txt"),"Actual attached process must wait for its bound approval");rejects<ToolAccessDenied>([&]{actual->invoke(command);});store.decide_operation(proposal.id,OperationDecision::allow,"fixture-controller").get();const auto result=task.result.get();const auto receipt=Json::parse(result.output_json);
            const bool exists=std::filesystem::exists(secondary/"owner-marker.txt");const auto marker=exists?read(secondary/"owner-marker.txt"):std::string{};
            std::cout<<"Process-owner actual receipt: "<<result.output_json<<'\n'<<"Process-owner actual folders: "<<Json{{"attached_marker_exists",exists},{"attached_marker_content",marker},{"primary_marker_exists",std::filesystem::exists(primary/"owner-marker.txt")}}.dump()<<'\n'<<std::flush;
            need(result.success&&receipt["exit_code"]==0&&receipt["termination"]=="exited"&&receipt["stdout"]["data"]=="owner-process-ok\r\n"&&marker=="actual attached process\n"&&!std::filesystem::exists(primary/"owner-marker.txt"),"Approved process must execute xlang3 in exactly its selected attached folder");}
        need(store.operations("run").get().size()==1,"Only the explicitly approved real command may create a durable operation");store.transition("run",RunState::running,RunState::completed).get();auto retired=command;retired.call_id="after-retirement";rejects<ToolAccessDenied>([&]{actual->invoke(retired);});
    }
    {PersistenceService reopened(database,{argv[1],argv[2]});need(reopened.operation(completed_operation).get().state==OperationState::succeeded&&reopened.run("run").get().state==RunState::completed&&read(secondary/"owner-marker.txt")=="actual attached process\n","Actual receipt and output must survive xlang3-backed SQLite reopen without replay");}
    std::cout<<"process tool owner contract passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
