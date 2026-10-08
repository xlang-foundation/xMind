#include "agentflow/mcp_tool_registry.hpp"
#include "agentflow/json_schema.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <thread>
using namespace agentflow;
using namespace std::chrono_literals;
namespace {
using Json=nlohmann::json;
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected MCP effect rejection did not occur");}
struct Directory {
    std::filesystem::path parent=std::filesystem::temp_directory_path(),path=parent/("xmind-mcp-effect-"+std::to_string(std::random_device{}()));
    Directory(){require(std::filesystem::create_directory(path),"Fixture root must be newly created");}
    ~Directory(){if(path.parent_path()==parent && path.filename().string().starts_with("xmind-mcp-effect-")){std::error_code ignored;std::filesystem::remove_all(path,ignored);}}
};
std::string contents(const std::filesystem::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};}
void start(PersistenceService& store,const std::string& run){store.create_session(run+"-session","labeled MCP effect fixture").get();store.start_prompt_run(run,run+"-session",R"({"content":"labeled MCP effect fixture"})").get();store.transition(run,RunState::queued,RunState::running).get();}
std::int64_t expiry(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+600000;}
Operation proposed(PersistenceService& store,const std::string& id){
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(std::chrono::steady_clock::now()<deadline){try{return store.operation(id).get();}catch(const NotFound&){std::this_thread::sleep_for(5ms);}}
    throw std::runtime_error("MCP proposal timed out");
}
struct Task {
    std::stop_source cancel;std::future<std::string> result;
    Task(PersistenceService& store,char** argv,std::filesystem::path root,std::string run,std::string id,std::string mode,std::string arguments=R"({"body":"actual native MCP effect\n","decimal":1.00000000000000000001})",std::optional<std::filesystem::path> shared_server_root={}):
        result(std::async(std::launch::async,[this,&store,argv,root,run,id,mode,arguments,shared_server_root]{
            const auto peer_root=shared_server_root.value_or(root);const auto config_id="fixture-server-"+peer_root.filename().string();
            WorkspaceTools workspace(root.string());McpStdioClient client({argv[1],argv[3],{argv[2],mode,(peer_root/"effect.txt").string(),(peer_root/"effect.marker").string()},{}});
            try {
                client.connect(std::chrono::steady_clock::now()+5s,cancel.get_token());
                McpToolRegistry registry(client,store,workspace,config_id,7,std::chrono::steady_clock::now()+5s,cancel.get_token());
                const auto definitions=registry.definitions();require(definitions.size()==1 && definitions[0].name.starts_with("mcp_") && definitions[0].name.size()==52,"Native registry must derive a bounded alias from trusted identity and exact snapshot");
                require(definitions[0].description.find("Peer tool name (untrusted metadata): \"fixture.write\"")!=std::string::npos,"Model catalogue must expose the original peer tool identity as quoted untrusted metadata");
                const auto alias=mode=="unknown-alias"?"fixture.write":definitions[0].name;
                const auto deadline=std::chrono::steady_clock::now()+(mode=="stopped-before-dispatch"?1500ms:mode=="timeout"?1500ms:5s);
                const auto output=registry.invoke(id,run,alias,arguments,expiry(),deadline,cancel.get_token());
                client.shutdown();require(client.status().exit_code==0,"Actual MCP peer must verify a valid exchange");return output;
            }catch(...){const auto error=std::current_exception();client.shutdown();require(client.status().exit_code==0,"Failure peer must accept the expected wire sequence and shutdown");std::rethrow_exception(error);}
        })){}
    ~Task(){cancel.request_stop();if(result.valid())result.wait();}
};
}
int main(int argc,char** argv){
    if(argc!=6)return 2;
    try {
        Directory directory;const auto database=(directory.path/"state.sqlite").string();const std::vector<std::string> imports{argv[4],argv[5]};
        auto root=[&](const std::string& id){const auto value=directory.path/id;require(std::filesystem::create_directory(value),"Isolated workspace must be newly created");std::ofstream(value/"effect.txt",std::ios::binary);return value;};
        std::filesystem::path uncertain_root,fault_root;
        {
            PersistenceService store(database,imports);
            for(const auto* mode:{"normal","legacy"}){
                const auto workspace=root(mode);start(store,mode);Task task(store,argv,workspace,mode,mode,mode);const auto proposal=proposed(store,mode);
                require(proposal.state==OperationState::awaiting_approval && contents(workspace/"effect.txt").empty(),"Untrusted readOnlyHint must never bypass real controller approval");
                require(task.result.wait_for(30ms)==std::future_status::timeout,"Waiting must not imply permission");
                const auto payload=Json::parse(proposal.spec.arguments_json);
                require(payload["server_config_id"]=="fixture-server-"+std::string(mode) && payload["config_revision"]==7 && payload["peer_tool"]=="fixture.write" && payload["catalogue_fingerprint"].get<std::string>().size()==64,"Approval must bind trusted configuration, peer name and schema snapshot");
                require(payload["arguments_json"].get<std::string>().find("1.00000000000000000001")!=std::string::npos,"Durable proposal must retain exact approved numeric bytes");
                store.decide_operation(mode,OperationDecision::allow,"fixture-controller").get();const auto actual=Json::parse(task.result.get());
                require(contents(workspace/"effect.txt")=="actual native MCP effect\n" && actual["acknowledged_by_peer"]==true && actual["independently_verified"]==false,"Real acknowledged tool effect must be distinguished from independent verification");
                const auto complete=store.operation(mode).get();require(complete.state==OperationState::succeeded && complete.decision_actor=="fixture-controller","Acknowledged effect must be durably attributed");
                Task duplicate(store,argv,workspace,mode,mode,mode);rejects<Conflict>([&]{duplicate.result.get();});require(contents(workspace/"effect.txt")=="actual native MCP effect\n","One operation identity must not replay an actual peer effect");
                store.transition(mode,RunState::running,RunState::completed).get();
            }
            for(const auto* mode:{"denied","cancel-wait","stopped-before-dispatch"}){
                const auto workspace=root(mode);start(store,mode);Task task(store,argv,workspace,mode,mode,mode);proposed(store,mode);
                if(std::string(mode)=="denied"){store.decide_operation(mode,OperationDecision::deny,"fixture-controller").get();rejects<PermissionDenied>([&]{task.result.get();});}
                else if(std::string(mode)=="cancel-wait"){task.cancel.request_stop();rejects<PermissionCancelled>([&]{task.result.get();});}
                else{std::this_thread::sleep_for(1600ms);store.decide_operation(mode,OperationDecision::allow,"fixture-controller").get();rejects<McpEffectNotDispatched>([&]{task.result.get();});require(store.operation(mode).get().state==OperationState::failed,"Known pre-dispatch stop must not invent uncertainty");}
                require(contents(workspace/"effect.txt").empty(),"Denied/cancelled/stopped proposal must not invoke the external tool");store.transition(mode,RunState::running,RunState::failed).get();
            }
            for(const auto* mode:{"bad-catalog-schema","duplicate-page","cursor-cycle","unknown-alias","invalid-arguments"}){
                const auto workspace=root(mode);start(store,mode);Task task(store,argv,workspace,mode,mode,mode,std::string(mode)=="invalid-arguments"?R"({"body":3,"decimal":1})":R"({"body":"blocked","decimal":1.00000000000000000001})");
                if(std::string(mode)=="duplicate-page" || std::string(mode)=="cursor-cycle")rejects<McpProtocolError>([&]{task.result.get();});else rejects<std::invalid_argument>([&]{task.result.get();});
                rejects<NotFound>([&]{store.operation(mode).get();});require(contents(workspace/"effect.txt").empty(),"Invalid schema/catalog/alias/arguments must not create approval or dispatch");store.transition(mode,RunState::running,RunState::failed).get();
            }
            for(const auto* mode:{"disconnect","timeout","cancel","rpc-error","tool-error","bad-output"}){
                const auto workspace=root(mode);if(std::string(mode)=="disconnect")uncertain_root=workspace;start(store,mode);Task task(store,argv,workspace,mode,mode,mode);proposed(store,mode);store.decide_operation(mode,OperationDecision::allow,"fixture-controller").get();
                if(std::string(mode)=="cancel"){
                    const auto deadline=std::chrono::steady_clock::now()+3s;while(!std::filesystem::exists(workspace/"effect.marker") && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(5ms);
                    require(std::filesystem::exists(workspace/"effect.marker"),"Cancellation fixture must observe actual dispatched effect first");task.cancel.request_stop();
                }
                rejects<McpEffectUncertain>([&]{task.result.get();});require(contents(workspace/"effect.txt")=="actual native MCP effect\n" && store.operation(mode).get().state==OperationState::uncertain,"Interrupted/error/invalid-result effects must remain actual, uncertain and unreplayed");store.transition(mode,RunState::running,RunState::failed).get();
                if(std::string(mode)=="tool-error")require(Json::parse(store.operation(mode).get().result_json)["reason"]=="mcp_peer_reported_tool_error","Actual peer tool error must remain distinct from unavailable validation");
                if(std::string(mode)=="bad-output")require(Json::parse(store.operation(mode).get().result_json)["reason"]=="mcp_output_schema_rejected","Actual schema rejection must remain distinct from a worker failure");
            }
            start(store,"blocked-run");Task blocked(store,argv,uncertain_root,"blocked-run","blocked-operation","normal");proposed(store,"blocked-operation");store.decide_operation("blocked-operation",OperationDecision::allow,"fixture-controller").get();rejects<WorkspaceEffectUncertain>([&]{blocked.result.get();});require(contents(uncertain_root/"effect.txt")=="actual native MCP effect\n","Workspace quarantine must block another actual MCP dispatch");store.transition("blocked-run",RunState::running,RunState::failed).get();
            const auto other_workspace=root("other-workspace");start(store,"blocked-other");Task other(store,argv,other_workspace,"blocked-other","blocked-other","normal",R"({"body":"must not run","decimal":1.00000000000000000001})",uncertain_root);proposed(store,"blocked-other");store.decide_operation("blocked-other",OperationDecision::allow,"fixture-controller").get();rejects<WorkspaceEffectUncertain>([&]{other.result.get();});require(contents(uncertain_root/"effect.txt")=="actual native MCP effect\n" && contents(other_workspace/"effect.txt").empty(),"Stable server quarantine must block actual dispatch from another workspace");store.transition("blocked-other",RunState::running,RunState::failed).get();
            fault_root=root("journal-fault");start(store,"journal-fault");
            {XlangSqlite inject(database,imports);inject.execute("CREATE TRIGGER reject_mcp_success BEFORE UPDATE OF state ON operations WHEN NEW.id='journal-fault' AND NEW.state='succeeded' BEGIN SELECT RAISE(ABORT,'fixture MCP outcome storage fault'); END");}
            Task fault(store,argv,fault_root,"journal-fault","journal-fault","normal");proposed(store,"journal-fault");store.decide_operation("journal-fault",OperationDecision::allow,"fixture-controller").get();rejects<McpOutcomeUnrecorded>([&]{fault.result.get();});require(contents(fault_root/"effect.txt")=="actual native MCP effect\n" && store.operation("journal-fault").get().state==OperationState::executing,"Outcome storage failure must preserve the real claimed effect for restart recovery");store.close();
        }
        {
            PersistenceService reopened(database,imports);require(reopened.operation("normal").get().state==OperationState::succeeded && reopened.operation("disconnect").get().state==OperationState::uncertain,"Acknowledged/uncertain peer outcomes must survive backend reopen");
            require(reopened.operation("journal-fault").get().state==OperationState::uncertain && reopened.run("journal-fault").get().state==RunState::failed,"Unrecorded claimed effect must be quarantined on restart");
            require(contents(fault_root/"effect.txt")=="actual native MCP effect\n","Restart must never replay a claimed external effect");reopened.close();
        }
        std::cout<<"Native approval-backed MCP registry passed actual subprocess file effects and embedded-xlang3 journal: exact approvals, untrusted hints, aliases/schema binding, denial/cancellation, duplicate rejection, malformed catalogs/arguments, post-effect lost/error replies, quarantine and journal-fault restart. No live model/UI/SDK interoperability or independent peer-result verification claimed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
