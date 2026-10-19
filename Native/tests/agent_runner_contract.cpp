#include "agentflow/agent_runner.hpp"
#include "agentflow/agent_permission_grants.hpp"
#include "agentflow/xlang_authoring_program.hpp"
#include "agentflow/xlang_python_config.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <cstdlib>
#include <algorithm>
#include <atomic>

using namespace agentflow;
using Json=nlohmann::json;
using namespace std::chrono_literals;
namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
std::string env(const char* name){const auto* value=std::getenv(name);return value?value:"";}
LanguageServerProfile rust_profile(const std::string& executable){
    LanguageServerProfile profile;profile.id="rust-analyzer";profile.executable=executable;profile.language_id="rust";profile.extensions={".rs"};profile.request_timeout=std::chrono::seconds(90);
    const auto path=env("PATH");if(!path.empty())profile.environment.emplace_back("PATH",std::filesystem::u8path(executable).parent_path().string()+";"+path);
    const auto rustup=env("RUSTUP_HOME");if(!rustup.empty())profile.environment.emplace_back("RUSTUP_HOME",rustup);
    const auto user=env("USERPROFILE");if(!user.empty()){profile.environment.emplace_back("USERPROFILE",user);profile.environment.emplace_back("HOME",user);profile.environment.emplace_back("CARGO_HOME",user+"/.cargo");}
    return profile;
}
void create_rust_project(const std::filesystem::path& root,const std::string& package,const std::string& symbol){
    std::filesystem::create_directories(root/"src");
    std::ofstream manifest(root/"Cargo.toml",std::ios::binary);manifest<<"[package]\nname = \""<<package<<"\"\nversion = \"0.1.0\"\nedition = \"2021\"\n[lib]\npath = \"src/lib.rs\"\n";manifest.close();
    std::ofstream source(root/"src/lib.rs",std::ios::binary);source<<"pub fn "<<symbol<<"() {}\n";
}
template<class Error,class Function> void rejects(Function action) {
    try {action();} catch(const Error&) {return;} throw std::runtime_error("Expected rejection did not occur");
}
bool streamed(PersistenceService& store,const std::string& id){
    for(const auto& event:store.events(id).get())if(event.kind=="model.text")return true;
    return false;
}
// Explicit model fixture: the real runner, filesystem and two independent
// xlang3 SQLite stores must use the admitted journal's folder snapshot.
class SplitJournalModel final:public AgentModelGateway {
public:
    explicit SplitJournalModel(std::string folder):folder_(std::move(folder)){}
    ModelCompletion complete(const ChatProviderConfig&,const ModelRequest& request,ChatCompletionStream::Sink,std::stop_token)override{
        ++calls;ModelCompletion reply;
        if(calls>1){
            require(request.messages.back().role==MessageRole::tool,"Split journal must retain its actual tool result");
            const auto result=Json::parse(request.messages.back().content);
            if(!result.contains("error")){
                require(result.at("content")=="Admitted journal folder\n"&&result.at("folder_id")==folder_&&result.at("workspace_revision")==7,"Split journal must use the admitted folder, not newer backend membership");
                reply.content="Observed the admitted journal's actual attached file.";reply.finish_reason="stop";return reply;
            }
            require(calls==2&&result["error"]["code"]=="repository_instructions_required","Split journal read failed before its admitted file was read");
        }
        require(calls<=2,"Split journal fixture exceeded its explicit guidance continuation");
        reply.finish_reason="tool_calls";reply.tool_calls.push_back({"split-journal-read-"+std::to_string(calls),"read_file",Json{{"path","note.txt"},{"folder_id",folder_}}.dump()});return reply;
    }
    ResponsesTokenCount count_context(const ChatProviderConfig&,const ModelRequest&,std::stop_token)override{throw std::runtime_error("Unexpected split-journal token count");}
    ResponsesCompactionResult compact_context(const ChatProviderConfig&,const ModelRequest&,std::stop_token)override{throw std::runtime_error("Unexpected split-journal compaction");}
    unsigned calls=0;
private:std::string folder_;
};
void split_journal_membership(PersistenceService& journal,const std::vector<std::string>& imports,const std::filesystem::path& root){
    const auto base=root.parent_path(),attached_path=base/"journal-attached",later_path=base/"journal-later";
    std::filesystem::create_directory(attached_path);std::filesystem::create_directory(later_path);
    std::ofstream(attached_path/"note.txt",std::ios::binary)<<"Admitted journal folder\n";
    WorkspaceTools primary(root.string()),attached(attached_path.string()),later(later_path.string());
    PersistenceService backend((base/"split-backend.sqlite").string(),imports);
    AgentSettings settings;settings.workspace=root.string();settings.a2a_enabled=false;settings.provider={"http://127.0.0.1:1/v1/chat/completions","explicit-split-journal",Capability::supported,Capability::supported};
    settings.workspace_membership_snapshot=[&]{return WorkspaceMembershipSnapshot{primary.identity(),8,{{primary.identity(),primary.root_path()},{later.identity(),later.root_path()}}};};
    const auto model=std::make_shared<SplitJournalModel>(attached.identity());
    AgentRunner runner(static_cast<AgentRunJournal&>(journal),backend,std::move(settings),{}, {},false,model);
    journal.create_session("split-journal","Independent journal workspace admission").get();runner.start("split-journal","split-journal","Read the previously admitted attached folder.");
    journal.append_event("split-journal","agent.workspace_membership",Json{{"workspace_id",primary.identity()},{"revision",7},{"folders",Json::array({{{"folder_id",primary.identity()},{"root",primary.root_path()}},{{"folder_id",attached.identity()},{"root",attached.root_path()}}})}}.dump()).get();
    require(runner.execute("split-journal").state==RunState::completed,"Split journal workspace admission must complete through its supplied journal");
    require(model->calls>=2&&model->calls<=3,"Split journal must receive actual model/tool continuation");
    const auto events=journal.events("split-journal").get();
    require(std::count_if(events.begin(),events.end(),[](const auto& event){return event.kind=="agent.workspace_membership";})==1,"Split journal must not replace or duplicate its admitted snapshot");
    rejects<NotFound>([&]{(void)backend.run("split-journal").get();});
}
Run cancel_after_stream(AgentRunner& runner,PersistenceService& store,const std::string& id){
    std::stop_source cancellation;
    // A bounded cleanup timer retires the real owner on fixture failure. The
    // normal cancellation below is anchored to observed provider traffic.
    std::jthread safety([&](std::stop_token ending){
        std::mutex mutex;std::condition_variable_any changed;std::unique_lock lock(mutex);
        changed.wait_for(lock,ending,5s,[]{return false;});
        if(!ending.stop_requested())cancellation.request_stop();
    });
    auto owner=std::async(std::launch::async,[&]{return runner.execute(id,cancellation.get_token());});
    const auto deadline=std::chrono::steady_clock::now()+4s;
    bool observed=false;
    while(std::chrono::steady_clock::now()<deadline&&owner.wait_for(0ms)!=std::future_status::ready){
        if(streamed(store,id)){observed=true;break;}
        std::this_thread::sleep_for(5ms);
    }
    cancellation.request_stop();const auto result=owner.get();safety.request_stop();
    require(observed,"Cancellation fixture must enter its actual provider stream before requesting stop");
    return result;
}
}
int main(int argc,char** argv) {
    if(argc!=7&&argc!=8) return 2;
    try {
        const std::vector<std::string> roots{argv[2],argv[3]};
        PersistenceService store(argv[1],roots);
        split_journal_membership(store,roots,std::filesystem::path(argv[4]));
        const std::string base=argv[5],synthetic="engine-protocol-test-not-a-real-key";
        SecretBytes secret({reinterpret_cast<const std::uint8_t*>(synthetic.data()),synthetic.size()});
        store.put_credential("test","provider","provider:chat","Synthetic test credential",std::move(secret),0).get();
        AgentSettings settings;settings.provider={base+"/main","fixture-deployment",Capability::supported,Capability::supported};
        settings.workspace=argv[4];settings.credential=CredentialReference{"test","provider","provider:chat"};
        store.create_session("main","Actual filesystem protocol fixture").get();
        auto gateway=make_persistence_agent_model_gateway(store,settings.credential);
        auto& run_journal=static_cast<AgentRunJournal&>(store);auto& context_journal=static_cast<AgentContextJournal&>(store);
        AgentRunner runner(run_journal,context_journal,store,settings,{}, {},false,std::move(gateway));runner.start("main","main","Read README");
        require(runner.execute("main").state==RunState::completed,"Actual provider/tool loop must complete its protocol turn");
        const auto history=store.history("main").get();
        require(history.size()==4 && history[1].role=="assistant" && history[2].role=="tool" && history[3].role=="assistant","Actual tool conversation must be persisted");
        const auto tool=Json::parse(Json::parse(history[2].json).at("content").get<std::string>());
        require(tool["content"]=="Actual file content written by fixture\n","Actual filesystem result must reach durable conversation");
        bool started=false,completed=false;
        for(const auto& event:store.events("main").get()) {if(event.kind=="tool.started") started=true;if(event.kind=="tool.completed") completed=true;}
        require(started && completed,"Actual tool execution events must persist");
        store.create_session("agent-permission-deny","Native agent permission denial fixture").get();auto permission_denied=settings;permission_denied.provider.endpoint=base+"/agent-permission-deny";permission_denied.permission_rules={{"read_file","README.md","deny"}};
        AgentRunner denied_runner(store,permission_denied);denied_runner.start("agent-permission-deny","agent-permission-deny","Read the workspace readme");require(denied_runner.execute("agent-permission-deny").state==RunState::completed,"A denied tool result must return to the actual model loop without dispatching the workspace read");
        const auto denied_history=store.history("agent-permission-deny").get();const auto denied_result=Json::parse(Json::parse(denied_history[2].json).at("content").get<std::string>());require(denied_result.at("error").at("code")=="agent_permission_denied","The native agent policy must reject the matching tool and resource before dispatch");
        bool policy_failed=false,policy_completed=false;for(const auto& event:store.events("agent-permission-deny").get()){if(event.kind=="tool.failed")policy_failed=true;if(event.kind=="tool.completed")policy_completed=true;}require(policy_failed&&!policy_completed,"A denied policy call must be journaled as failed, never as a successful tool result");
        store.create_session("agent-permission-allow","Native ordered agent permission fixture").get();auto permission_allow=settings;permission_allow.provider.endpoint=base+"/agent-permission-allow";permission_allow.permission_rules={{"read_file","README.md","deny"},{"read_file","README.md","allow"}};
        AgentRunner allow_runner(store,permission_allow);allow_runner.start("agent-permission-allow","agent-permission-allow","Read the workspace readme");require(allow_runner.execute("agent-permission-allow").state==RunState::completed,"A later agent allow rule must resolve the ordered policy and return to the model loop");
        const auto allow_history=store.history("agent-permission-allow").get();const auto allowed_result=Json::parse(Json::parse(allow_history[2].json).at("content").get<std::string>());require(allowed_result.at("content")=="Actual file content written by fixture\n","An ordered per-agent allow may override an earlier matching agent deny");
        store.create_session("agent-permission-ceiling","Inherited native permission ceiling fixture").get();auto permission_ceiling=settings;permission_ceiling.provider.endpoint=base+"/agent-permission-deny";permission_ceiling.permission_rules={{"read_file","README.md","deny"}};
        AgentRunner ceiling_runner(store,permission_ceiling);ceiling_runner.start("agent-permission-ceiling","agent-permission-ceiling","Do not read the workspace readme");require(ceiling_runner.execute("agent-permission-ceiling",{}, {}, {}, {}, {},false,{{"read_file","README.md","allow"}}).state==RunState::completed,"A child/profile allow cannot override an inherited permission ceiling");
        const auto ceiling_history=store.history("agent-permission-ceiling").get();const auto ceiling_result=Json::parse(Json::parse(ceiling_history[2].json).at("content").get<std::string>());require(ceiling_result.at("error").at("code")=="agent_permission_denied","Inherited denies must remain hard constraints when profile rules include allow");
        const auto run_asked_read=[&](const std::string& session,const std::string& route,bool allow){
            store.create_session(session,"Native one-shot permission ask fixture").get();auto ask_settings=settings;ask_settings.provider.endpoint=base+route;ask_settings.permission_rules={{"read_file","README.md","ask"}};
            AgentRunner ask_runner(store,ask_settings);ask_runner.start(session,session,"Read the workspace readme");std::stop_source cancellation;
            auto execution=std::async(std::launch::async,[&]{return ask_runner.execute(session,cancellation.get_token());});
            std::optional<Operation> proposal;const auto deadline=std::chrono::steady_clock::now()+5s;
            while(std::chrono::steady_clock::now()<deadline&&!proposal){
                if(execution.wait_for(0ms)==std::future_status::ready)throw std::runtime_error("Ask-policy read ended before controller review");
                for(const auto& operation:store.operations(session).get())if(operation.state==OperationState::awaiting_approval){proposal=operation;break;}
                if(!proposal)std::this_thread::sleep_for(5ms);
            }
            if(!proposal){cancellation.request_stop();(void)execution.get();throw std::runtime_error("Ask-policy read did not publish a durable approval request");}
            require(proposal->spec.run_id==session&&proposal->spec.tool=="read_file"&&Json::parse(proposal->spec.arguments_json)==Json{{"path","README.md"}},"Ask request must bind the exact owning run, tool and arguments");
            require(store.history(session).get().size()==1,"Asked read must remain undispatched until the controller decides");
            store.decide_operation(proposal->id,allow?OperationDecision::allow:OperationDecision::deny,"fixture-controller").get();
            require(execution.get().state==RunState::completed,"The model loop must resume after a one-shot permission decision");
            const auto saved=store.operation(proposal->id).get();
            if(allow){require(saved.state==OperationState::succeeded&&Json::parse(saved.result_json).at("content")=="Actual file content written by fixture\n","Allowed read must execute once and durably settle the asked operation");const auto history=store.history(session).get();const auto result=Json::parse(Json::parse(history[2].json).at("content").get<std::string>());require(result.at("content")=="Actual file content written by fixture\n","Allowed read output must continue to the model");}
            else{require(saved.state==OperationState::denied,"Denied permission ask must remain durably denied");const auto history=store.history(session).get();const auto result=Json::parse(Json::parse(history[2].json).at("content").get<std::string>());require(result.at("error").at("code")=="permission_denied","Denied read must return a denied result without dispatching the workspace tool");}
        };
        run_asked_read("agent-permission-ask","/agent-permission-ask",true);
        run_asked_read("agent-permission-ask-deny","/agent-permission-ask-deny",false);
        {
            const std::string session="agent-permission-reusable",first_run="agent-permission-reusable-first",second_run="agent-permission-reusable-second";
            store.create_session(session,"Reusable exact-scope permission fixture").get();auto reusable_settings=settings;reusable_settings.provider.endpoint=base+"/agent-permission-ask-reuse";reusable_settings.permission_rules={{"read_file","README.md","ask"}};
            AgentRunner first_runner(store,reusable_settings);first_runner.start(first_run,session,"Read the workspace readme");std::stop_source cancellation;
            auto first_execution=std::async(std::launch::async,[&]{return first_runner.execute(first_run,cancellation.get_token());});std::optional<Operation> proposal;const auto deadline=std::chrono::steady_clock::now()+5s;
            while(std::chrono::steady_clock::now()<deadline&&!proposal){if(first_execution.wait_for(0ms)==std::future_status::ready)throw std::runtime_error("Reusable permission fixture ended before review");for(const auto& operation:store.pending_operations_for_session(session).get())if(operation.spec.run_id==first_run){proposal=operation;break;}if(!proposal)std::this_thread::sleep_for(5ms);}
            if(!proposal){cancellation.request_stop();(void)first_execution.get();throw std::runtime_error("Reusable permission fixture did not publish its approval request");}
            const auto permission_request=AgentPermissionGrantStore(store).request(proposal->id);require(permission_request&&permission_request->session_id==session&&permission_request->action=="read_file"&&permission_request->resource=="README.md","The native approval must retain its exact session and resource scope");
            AgentPermissionGrantStore(store).grant(session,proposal->id);store.decide_operation(proposal->id,OperationDecision::allow,"fixture-controller").get();require(first_execution.get().state==RunState::completed,"The explicitly granted first read must finish normally");
            AgentRunner second_runner(store,reusable_settings);second_runner.start(second_run,session,"Read the workspace readme again");require(second_runner.execute(second_run).state==RunState::completed,"A saved exact-scope grant must authorize the next matching read in the same conversation");
            const auto second_operations=store.operations(second_run).get();require(second_operations.size()==1&&second_operations.front().state==OperationState::succeeded&&second_operations.front().decision_actor=="agent-permission-grant","The reused read must remain durably journaled with its grant decision actor");
            require(!AgentPermissionGrantStore(store).allows("another-session",permission_request->workspace_id,permission_request->policy_binding,permission_request->action,permission_request->resource),"A saved grant must not authorize another conversation");
        }
        const auto attached_root=std::filesystem::path(argv[4]).parent_path()/L"attached";
        WorkspaceTools primary(argv[4]),attached(attached_root.string());
        const auto primary_id=primary.identity(),attached_id=attached.identity(),primary_root=primary.root_path(),attached_canonical=attached.root_path();
        auto multi=settings;multi.provider.endpoint=base+"/multi-folder?folder_id="+attached_id;
        multi.workspace_membership_snapshot=[=]{return WorkspaceMembershipSnapshot{primary_id,2,{{primary_id,primary_root},{attached_id,attached_canonical}}};};
        store.create_session("multi-folder","Admitted multi-folder read fixture").get();AgentRunner multi_runner(store,multi);multi_runner.start("multi-folder","multi-folder","Read the attached folder explicitly");
        require(multi_runner.execute("multi-folder").state==RunState::completed,"AgentRunner must dispatch an explicit secondary-folder read through its captured membership");
        const auto multi_history=store.history("multi-folder").get();require(multi_history.size()==4,"Folder-qualified native reads must remain in the normal persisted tool loop");
        const auto multi_result=Json::parse(Json::parse(multi_history[2].json).at("content").get<std::string>());
        require(multi_result.at("content")=="Attached folder content\n"&&multi_result.at("folder_id")==attached_id&&multi_result.at("workspace_id")==primary_id&&multi_result.at("workspace_revision")==2,"Tool result must identify the exact attached member and admitted revision");
        bool captured_membership=false;for(const auto& event:store.events("multi-folder").get())if(event.kind=="agent.workspace_membership"){
            const auto membership=Json::parse(event.json);captured_membership=membership.at("revision")==2&&membership.at("folders").size()==2&&membership.at("folders")[1].at("root")==attached_canonical;
        }require(captured_membership,"Run admission must durably retain the exact folder identities and roots used by child executions");
        if(argc==8){
            const auto profile=rust_profile(argv[6]);create_rust_project(std::filesystem::u8path(primary_root),"agentflow_lsp_primary","primary_symbol");create_rust_project(std::filesystem::u8path(attached_canonical),"agentflow_lsp_attached","attached_symbol");
            const auto late_root=(std::filesystem::path(argv[4]).parent_path()/L"late-attached").string();WorkspaceTools late(late_root);const auto late_id=late.identity(),late_canonical=late.root_path();std::atomic_bool late_attached=false;
            create_rust_project(std::filesystem::u8path(late_canonical),"agentflow_lsp_late","late_symbol");
            auto lsp=multi;lsp.provider.endpoint=base+"/multi-folder-lsp?folder_id="+attached_id+"&late_folder_id="+late_id;lsp.language_servers.push_back(profile);
            lsp.workspace_membership_snapshot=[=,&late_attached]{WorkspaceMembershipSnapshot snapshot{primary_id,late_attached?3:2,{{primary_id,primary_root},{attached_id,attached_canonical}}};if(late_attached)snapshot.folders.push_back({late_id,late_canonical});return snapshot;};
            lsp.language_server_dispatch=[profile,&late_attached,late_id](const std::string& server_id,const LanguageServerRequest& request,const std::vector<WorkspaceToolFolder>& folders){
                if(folders.size()==2&&std::any_of(folders.begin(),folders.end(),[&](const auto& member){return member.folder_id==late_id;}))throw std::runtime_error("A folder attached during the provider turn must not enter the run's admitted snapshot");
                late_attached=true;
                const auto selected=std::find_if(folders.begin(),folders.end(),[&](const auto& member){return member.folder_id==request.folder_id;});
                if(selected==folders.end())throw ToolAccessDenied("Model language-server folder is not admitted for this run");
                if(server_id!=profile.id)throw ToolAccessDenied("Model selected an unregistered language-server profile");
                WorkspaceTools selected_workspace(selected->root);LanguageServerToolRegistry selected_registry(selected_workspace,{profile});auto scoped=request;scoped.folder_id.clear();auto result=Json::parse(selected_registry.query(scoped,server_id));result["folder_id"]=selected->folder_id;return result.dump();
            };
            store.create_session("multi-folder-lsp","Agent model language-server query in an admitted folder").get();AgentRunner lsp_runner(store,lsp);lsp_runner.start("multi-folder-lsp","multi-folder-lsp","Inspect the attached Rust file with the language server");
            require(lsp_runner.execute("multi-folder-lsp").state==RunState::completed,"AgentRunner must complete a provider-issued language-server tool call against the admitted secondary folder");
            const auto lsp_history=store.history("multi-folder-lsp").get();require(lsp_history.size()==6,"Model language-server success, late-folder rejection and final answer must persist across the real tool loop");
            const auto lsp_result=Json::parse(Json::parse(lsp_history[2].json).at("content").get<std::string>());require(lsp_result.at("folder_id")==attached_id&&lsp_result.at("result").is_array()&&!lsp_result.at("result").empty()&&lsp_result.at("result")[0].at("name")=="attached_symbol","AgentRunner must return real Rust Analyzer symbols from the exact attached-folder root");
            const auto late_rejection=Json::parse(Json::parse(lsp_history[4].json).at("content").get<std::string>());require(late_rejection.at("error").at("code")=="access_denied","A folder attached after this run started must be rejected from its captured language-server tool schema");
            const auto next_membership=lsp.workspace_membership_snapshot();require(next_membership.revision==3&&next_membership.folders.size()==3&&next_membership.folders.back().folder_id==late_id,"A later run must see the newly attached folder in the newer membership revision");
            auto lsp_later=lsp;lsp_later.provider.endpoint=base+"/multi-folder-lsp-later?folder_id="+late_id;store.create_session("multi-folder-lsp-later","Later agent run sees the newly attached folder").get();AgentRunner lsp_later_runner(store,lsp_later);lsp_later_runner.start("multi-folder-lsp-later","multi-folder-lsp-later","Inspect the newly attached Rust folder");
            require(lsp_later_runner.execute("multi-folder-lsp-later").state==RunState::completed,"A later AgentRunner run must query the newly admitted folder");const auto later_history=store.history("multi-folder-lsp-later").get();require(later_history.size()==4,"Later folder query and model response must persist as one native tool round-trip");
            const auto later_result=Json::parse(Json::parse(later_history[2].json).at("content").get<std::string>());require(later_result.at("folder_id")==late_id&&later_result.at("result")[0].at("name")=="late_symbol","The next admitted run must use the new folder root and its own Rust Analyzer symbol");
            bool revision_three=false;for(const auto& event:store.events("multi-folder-lsp-later").get())if(event.kind=="agent.workspace_membership")revision_three=Json::parse(event.json).at("revision")==3;require(revision_three,"The later model tool call must be durably tied to workspace membership revision 3");
        }
        const auto* system_root=std::getenv("SystemRoot");if(!system_root||!*system_root)throw std::runtime_error("SystemRoot is required for the native Windows process fixture");
        const auto powershell=(std::filesystem::u8path(system_root)/L"System32"/L"WindowsPowerShell"/L"v1.0"/L"powershell.exe").string();
        auto multi_process=multi;multi_process.provider.endpoint=base+"/multi-folder-process?folder_id="+attached_id;
        multi_process.process_profiles.push_back({"powershell",powershell,1,{"-NoLogo","-NoProfile","-NonInteractive","-Command"},std::chrono::seconds(30),{},true});
        const std::string process_path="agent-process-folder.txt";const std::string command="[IO.File]::WriteAllText('"+process_path+"',(Get-Location).Path)";
        store.create_session("multi-process","Agent process targets the captured secondary folder").get();AgentRunner process_runner(store,multi_process);process_runner.start("multi-process","multi-process","Run a process in the explicitly attached folder");
        const auto process_run=process_runner.execute("multi-process");
        if(process_run.state!=RunState::completed){
            Json operations=Json::array(),events=Json::array();
            for(const auto& operation:store.operations("multi-process").get())operations.push_back({{"id",operation.id},{"state",to_string(operation.state)},{"result_json",operation.result_json}});
            for(const auto& event:store.events("multi-process").get())events.push_back({{"kind",event.kind},{"json",event.json}});
            throw std::runtime_error("AgentRunner attached process failed: "+Json{{"state",to_string(process_run.state)},{"operations",std::move(operations)},{"events",std::move(events)}}.dump().substr(0,65536));
        }
        const auto attached_process_effect=std::filesystem::u8path(attached_canonical)/process_path;const auto primary_process_effect=std::filesystem::u8path(primary_root)/process_path;
        require(std::filesystem::is_regular_file(attached_process_effect)&&!std::filesystem::exists(primary_process_effect),"Agent process must create its actual effect only in the explicitly selected attached folder");
        {std::ifstream effect(attached_process_effect,std::ios::binary);std::string location;std::getline(effect,location);require(std::filesystem::path(location).lexically_normal()==std::filesystem::path(attached_canonical).lexically_normal(),"Agent process cwd must be the selected attached folder root");}
        const auto process_operations=store.operations("multi-process").get();require(process_operations.size()==1&&process_operations.front().state==OperationState::succeeded&&process_operations.front().decision_actor=="trusted-process-profile:powershell","Agent process must persist successful trusted-profile grant attribution");
        const auto process_history=store.history("multi-process").get();require(process_history.size()==4&&Json::parse(Json::parse(process_history[2].json).at("content").get<std::string>()).at("exit_code")==0,"Agent process output and result must round-trip through persisted tool history");
        auto multi_patch=multi;multi_patch.approved_edits=true;multi_patch.provider.endpoint=base+"/multi-folder-patch?folder_id="+attached_id;
        store.create_session("multi-folder-patch","Agent patch targets a captured secondary folder").get();AgentRunner patch_runner(store,multi_patch);patch_runner.start("multi-folder-patch","multi-folder-patch","Apply one approved patch to the explicitly attached folder");
        auto patch_run=std::async(std::launch::async,[&]{return patch_runner.execute("multi-folder-patch");});std::optional<Operation> patch_proposal;
        const auto patch_deadline=std::chrono::steady_clock::now()+5s;while(std::chrono::steady_clock::now()<patch_deadline&&!patch_proposal){for(const auto& operation:store.operations("multi-folder-patch").get())if(operation.state==OperationState::awaiting_approval){patch_proposal=operation;break;}if(!patch_proposal)std::this_thread::sleep_for(5ms);}
        require(patch_proposal.has_value(),"Agent patch must reach its actual per-file approval before the filesystem effect");const auto patch_payload=Json::parse(patch_proposal->spec.arguments_json);require(patch_proposal->spec.workspace==primary_id&&patch_payload.at("workspace_id")==primary_id&&patch_payload.at("workspace_revision")==2&&patch_payload.at("folder_id")==attached_id&&patch_payload.at("target_workspace_id")==attached_id,"Agent patch approval must bind the captured workspace revision and selected folder identity");
        store.decide_operation(patch_proposal->id,OperationDecision::allow,"fixture-controller").get();require(patch_run.get().state==RunState::completed,"Approved agent patch must continue through the real model/tool loop");
        const auto attached_patch_effect=std::filesystem::u8path(attached_canonical)/L"agent-patches"/L"secondary.txt";const auto primary_patch_effect=std::filesystem::u8path(primary_root)/L"agent-patches"/L"secondary.txt";require(std::filesystem::is_regular_file(attached_patch_effect)&&!std::filesystem::exists(primary_patch_effect),"Approved agent patch must create only beneath its selected attached folder");
        const auto patch_history=store.history("multi-folder-patch").get();require(patch_history.size()==4&&Json::parse(Json::parse(patch_history[2].json).at("content").get<std::string>()).at("state")=="succeeded","Agent patch receipt must reach durable model continuation");
        auto multi_root_patch=multi;multi_root_patch.approved_edits=true;multi_root_patch.provider.endpoint=base+"/multi-root-patch?primary_id="+primary_id+"&attached_id="+attached_id;
        store.create_session("multi-root-patch","One reviewed coding patch spans admitted workspace folders").get();AgentRunner multi_root_runner(store,multi_root_patch);multi_root_runner.start("multi-root-patch","multi-root-patch","Apply one approved patch across the primary and attached folders");
        auto multi_root_run=std::async(std::launch::async,[&]{return multi_root_runner.execute("multi-root-patch");});const auto wait_approval=[&](auto& run_future,const std::string& session,std::size_t index){std::optional<Operation> proposal;const auto deadline=std::chrono::steady_clock::now()+5s;while(std::chrono::steady_clock::now()<deadline&&!proposal){if(run_future.wait_for(0ms)==std::future_status::ready)throw std::runtime_error("Cross-folder patch ended before approval "+std::to_string(index)+": "+to_string(run_future.get().state));for(const auto& operation:store.operations(session).get())if(operation.state==OperationState::awaiting_approval){const auto value=Json::parse(operation.spec.arguments_json);if(value.value("patch_index",std::size_t{})==index){proposal=operation;break;}}if(!proposal)std::this_thread::sleep_for(5ms);}require(proposal.has_value(),"Cross-folder patch must request each ordered per-file approval");return *proposal;};
        const auto primary_proposal=wait_approval(multi_root_run,"multi-root-patch",0);const auto primary_patch_payload=Json::parse(primary_proposal.spec.arguments_json);require(primary_patch_payload.at("folder_id")==primary_id&&primary_patch_payload.at("workspace_revision")==2&&primary_patch_payload.at("path")=="agent-patches/primary.txt"&&!std::filesystem::exists(std::filesystem::u8path(primary_root)/L"agent-patches"/L"primary.txt"),"First cross-folder approval must bind only the primary member before writing");
        store.decide_operation(primary_proposal.id,OperationDecision::allow,"fixture-controller").get();const auto attached_proposal=wait_approval(multi_root_run,"multi-root-patch",0);const auto attached_patch_payload=Json::parse(attached_proposal.spec.arguments_json);require(attached_patch_payload.at("folder_id")==attached_id&&attached_patch_payload.at("workspace_revision")==2&&attached_patch_payload.at("path")=="agent-patches/attached.txt"&&std::filesystem::is_regular_file(std::filesystem::u8path(primary_root)/L"agent-patches"/L"primary.txt")&&!std::filesystem::exists(std::filesystem::u8path(attached_canonical)/L"agent-patches"/L"attached.txt"),"Second cross-folder approval must bind the attached member after the first effect");
        store.decide_operation(attached_proposal.id,OperationDecision::allow,"fixture-controller").get();require(multi_root_run.get().state==RunState::completed,"Cross-folder patch must return through the actual model/tool loop");require(std::filesystem::is_regular_file(std::filesystem::u8path(primary_root)/L"agent-patches"/L"primary.txt")&&std::filesystem::is_regular_file(std::filesystem::u8path(attached_canonical)/L"agent-patches"/L"attached.txt"),"One cross-folder proposal must apply each approved file only to its own registered root");
        const auto multi_root_history=store.history("multi-root-patch").get();require(multi_root_history.size()==4,"Cross-folder patch report must persist in task history");const auto multi_root_report=Json::parse(Json::parse(multi_root_history[2].json).at("content").get<std::string>());require(multi_root_report.at("state")=="succeeded"&&multi_root_report.at("atomic")==false&&multi_root_report.at("requested_folders")==2&&multi_root_report.at("processed_folders")==2&&multi_root_report.at("requested_files")==2&&multi_root_report.at("succeeded_files")==2&&multi_root_report.at("folder_results").size()==2&&multi_root_report.at("folder_results")[0].at("folder_id")==primary_id&&multi_root_report.at("folder_results")[1].at("folder_id")==attached_id,"Combined receipt must report both selected roots and exact completed per-folder outcomes");const auto multi_root_operations=store.operations("multi-root-patch").get();require(multi_root_operations.size()==2&&std::all_of(multi_root_operations.begin(),multi_root_operations.end(),[](const auto& operation){return operation.state==OperationState::succeeded;}),"Both cross-folder effects must retain independent durable approvals and outcomes");
        auto multi_root_deny=multi;multi_root_deny.approved_edits=true;multi_root_deny.provider.endpoint=base+"/multi-root-patch-deny?primary_id="+primary_id+"&attached_id="+attached_id;
        store.create_session("multi-root-patch-deny","Cross-folder partial denial retains only an approved earlier root").get();AgentRunner deny_root_runner(store,multi_root_deny);deny_root_runner.start("multi-root-patch-deny","multi-root-patch-deny","Keep the approved primary edit and reject the attached edit");
        auto deny_root_run=std::async(std::launch::async,[&]{return deny_root_runner.execute("multi-root-patch-deny");});const auto keep_primary=wait_approval(deny_root_run,"multi-root-patch-deny",0);const auto keep_payload=Json::parse(keep_primary.spec.arguments_json);require(keep_payload.at("folder_id")==primary_id&&keep_payload.at("path")=="agent-patches/partial-primary.txt","Partial cross-folder batch must start in the explicitly ordered primary folder");store.decide_operation(keep_primary.id,OperationDecision::allow,"fixture-controller").get();const auto deny_attached=wait_approval(deny_root_run,"multi-root-patch-deny",0);const auto deny_payload=Json::parse(deny_attached.spec.arguments_json);require(deny_payload.at("folder_id")==attached_id&&deny_payload.at("path")=="agent-patches/partial-attached.txt","Second partial-batch approval must target its attached root");store.decide_operation(deny_attached.id,OperationDecision::deny,"fixture-controller").get();require(deny_root_run.get().state==RunState::completed,"A denied later root must return its accurate partial report through the model");const auto partial_primary=std::filesystem::u8path(primary_root)/L"agent-patches"/L"partial-primary.txt",partial_attached=std::filesystem::u8path(attached_canonical)/L"agent-patches"/L"partial-attached.txt";require(std::filesystem::is_regular_file(partial_primary)&&!std::filesystem::exists(partial_attached),"Cross-folder denial must preserve only the prior approved effect");
        const auto deny_history=store.history("multi-root-patch-deny").get();const auto denied_report=Json::parse(Json::parse(deny_history[2].json).at("content").get<std::string>());require(deny_history.size()==4&&denied_report.at("state")=="partial"&&denied_report.at("requested_files")==2&&denied_report.at("succeeded_files")==1&&denied_report.at("processed_folders")==2&&denied_report.at("stop_reason")=="permission_denied"&&denied_report.at("folder_results").size()==2&&denied_report.at("not_requested_folders").empty(),"A later-root denial must be reported as partial with no hidden or unrequested folder effect");const auto denied_operations=store.operations("multi-root-patch-deny").get();require(denied_operations.size()==2&&denied_operations[0].state==OperationState::succeeded&&denied_operations[1].state==OperationState::denied,"Successful and denied cross-root effects must retain separate durable decisions");
        // Real model/tool-loop regression: explicit attached guidance must use
        // that admitted folder and reach the following provider request.
        const auto attached_guide=std::filesystem::u8path(attached_canonical)/"guide";
        std::filesystem::create_directory(attached_guide);
        {std::ofstream root(std::filesystem::u8path(attached_canonical)/"AGENTS.md",std::ios::binary);root<<"Attached root guidance marker.\n";}
        {std::ofstream nested(attached_guide/"AGENTS.md",std::ios::binary);nested<<"Attached nested guidance marker.\n";}
        auto guidance_settings=multi;guidance_settings.provider.endpoint=base+"/attached-guidance?folder_id="+attached_id;
        store.create_session("attached-guidance","Explicit attached repository guidance").get();AgentRunner guidance_runner(store,guidance_settings);
        guidance_runner.start("attached-guidance","attached-guidance","Read the admitted attached folder guidance and reject an unregistered folder");
        require(guidance_runner.execute("attached-guidance").state==RunState::completed,"AgentRunner must route explicit attached guidance through its exact admitted folder");
        const auto guidance_history=store.history("attached-guidance").get();require(guidance_history.size()==5,"Both guidance results must be committed with their model tool turn");
        const auto guidance_result=Json::parse(Json::parse(guidance_history[2].json).at("content").get<std::string>());
        require(guidance_result.at("folder_id")==attached_id&&guidance_result.at("workspace_id")==primary_id&&guidance_result.at("workspace_revision")==2&&guidance_result.at("sources").size()==2,"Guidance result must bind the admitted attached folder and membership revision");
        require(Json::parse(Json::parse(guidance_history[3].json).at("content").get<std::string>()).at("error").at("code")=="access_denied","Guidance must reject a folder outside the admitted snapshot");
        require(store.operations("attached-guidance").get().empty(),"Guidance discovery and rejected membership must not propose effects");
        const auto code_root_path=std::filesystem::absolute(std::filesystem::u8path(argv[4])).u8string();
        const std::string code_root(reinterpret_cast<const char*>(code_root_path.data()),code_root_path.size());
        const auto authored=run_xlang_authoring_program(argv[2],argv[3],code_root,"fixture_tools");
        const auto imported=import_xlang_python_tool_config(store,authored.python_tools_config_json);
        const auto registered=load_xlang_python_tool_config(store);
        require(imported.tools.size()==1&&registered.code_root==code_root&&registered.tools.size()==1&&registered.tools[0].name=="python_uppercase","AgentRunner tools must originate from the xlang3 authoring API and load from SQLite");
        store.create_session("python-tool","Embedded xlang3 model tool fixture").get();auto python=settings;python.provider.endpoint=base+"/python-tool";
        for(const auto& tool:registered.tools)python.xlang_python_tools.push_back({ModelToolDefinition{tool.name,tool.description,tool.input_schema_json},XlangPythonCallable(argv[2],argv[3],registered.code_root,tool.module,tool.function)});
        AgentRunner python_runner(store,python);python_runner.start("python-tool","python-tool","Call the registered Python tool on xMind");
        require(python_runner.execute("python-tool").state==RunState::completed,"Registered xlang3 callable must execute through the actual model/tool loop");
        const auto python_history=store.history("python-tool").get();require(python_history.size()==4,"Actual xlang3 tool result and model continuation must be persisted together");
        const auto python_result=Json::parse(Json::parse(python_history[2].json).at("content").get<std::string>());require(python_result==Json{{"value","XMIND"}},"Actual xlang3 result must reach the model as JSON tool content");
        auto worker_catalogue=Json::parse(authored.python_tools_config_json);worker_catalogue["tools"][0]["execution"]="worker";
        const auto registered_worker=import_xlang_python_tool_config(store,worker_catalogue.dump());
        store.create_session("python-tool-worker","xlang3 worker tool fixture").get();auto worker_settings=settings;worker_settings.provider.endpoint=base+"/python-tool-worker";
        for(const auto& tool:registered_worker.tools)worker_settings.xlang_python_tools.push_back({ModelToolDefinition{tool.name,tool.description,tool.input_schema_json},XlangPythonCallable(argv[2],argv[3],registered_worker.code_root,tool.module,tool.function,argv[argc-1]),tool.execution=="worker"?XlangPythonExecution::worker:XlangPythonExecution::in_process});
        AgentRunner worker_runner(store,worker_settings);worker_runner.start("python-tool-worker","python-tool-worker","Run the registered tool in the local xlang3 worker");
        require(worker_runner.execute("python-tool-worker").state==RunState::completed,"Registered worker callable must execute through the actual agent/model tool loop");
        const auto worker_history=store.history("python-tool-worker").get();require(worker_history.size()==4,"Actual worker tool result and model continuation must be persisted together");
        const auto worker_result=Json::parse(Json::parse(worker_history[2].json).at("content").get<std::string>());require(worker_result==Json{{"value","XMIND"}},"Shared-memory worker result must reach the model as JSON tool content");
        store.create_session("python-tool-filtered","Named agent xlang3 allowlist fixture").get();auto filtered=python;filtered.provider.endpoint=base+"/python-tool-filtered";filtered.tool_allowlist=std::vector<std::string>{"read_file"};
        AgentRunner filtered_runner(store,filtered);filtered_runner.start("python-tool-filtered","python-tool-filtered","Do not expose the registered Python tool");
        require(filtered_runner.execute("python-tool-filtered").state==RunState::completed,"Agent allowlists must narrow the registered xlang3 tool catalogue");
        store.create_session("skill","Native skill execution fixture").get();auto skilled=settings;skilled.provider.endpoint=base+"/skill";AgentRunner skill_runner(store,skilled);skill_runner.start("skill","skill","Load the synthetic workspace skill and inspect its actual companion file");
        require(skill_runner.execute("skill").state==RunState::completed,"Native skill loading must complete its actual model/tool loop");const auto skill_history=store.history("skill").get();require(skill_history.size()==7,"Actual skill turn and same-batch deferral must persist as one matched conversation");
        const auto blocked=Json::parse(Json::parse(skill_history[3].json).at("content").get<std::string>());require(blocked.at("error").at("code")=="repository_instructions_required","Companion read in the activation batch must wait for delivered skill guidance");
        const auto companion=Json::parse(Json::parse(skill_history[5].json).at("content").get<std::string>());require(companion.at("content")=="Actual native skill companion bytes\n","Delivered skill must use actual authorized native file reads");bool skill_binding=false;for(const auto& event:store.events("skill").get())if(event.kind=="agent.repository_scope"&&Json::parse(event.json).contains("skills"))skill_binding=true;require(skill_binding,"Actual run must retain Native skill snapshot metadata");
        store.create_session("skill-capacity","Native instruction capacity fixture").get();auto capacity=settings;capacity.provider.endpoint=base+"/skill-capacity";capacity.instructions="Synthetic budget guard fixture "+std::string(53000,'x');AgentRunner capacity_runner(store,capacity);capacity_runner.start("skill-capacity","skill-capacity","Synthetic request to load a guide beyond the remaining instruction budget");require(capacity_runner.execute("skill-capacity").state==RunState::completed,"An oversized guide load must return a tool rejection and allow the actual agent loop to continue");const auto capacity_history=store.history("skill-capacity").get();require(capacity_history.size()==4,"Rejected activation must have a complete matched durable tool turn");const auto capacity_error=Json::parse(Json::parse(capacity_history[2].json).at("content").get<std::string>());require(capacity_error.at("error").at("code")=="file_unavailable","Remaining native instruction capacity must be checked before acknowledging guide activation");for(const auto& event:store.events("skill-capacity").get())if(event.kind=="agent.repository_scope")require(!Json::parse(event.json).contains("skills"),"Rejected activation must never become delivered skill metadata");
        auto followed=skilled;followed.provider.endpoint=base+"/skill-follow-up";AgentRunner following(store,followed);following.start("skill-follow-up","skill","Continue using the previously activated native skill");require(following.execute("skill-follow-up").state==RunState::completed,"A continued prompt must restore skill guidance before its first provider request");require(store.run_skills("skill-follow-up").get().ids==std::vector<std::string>{"inspect"},"Actual continued agent must retain its committed native selection");
        WorkspaceTools manual_workspace(argv[4]);store.create_session("manual-user","Synthetic user attached disabled guide").get();const SkillSelections manual_selection{manual_workspace.identity(),{"manual"},{"manual"}};const auto selected=store.replace_session_skills("manual-user",manual_selection,0).get();auto manual_settings=settings;manual_settings.provider.endpoint=base+"/manual-attached";AgentRunner manual_runner(store,manual_settings);manual_runner.start("manual-attached","manual-user","Follow the currently user-attached skill");require(manual_runner.execute("manual-attached").state==RunState::completed,"User attachment must reach the first actual provider boundary and authorized companion read");require(store.run_skills("manual-attached").get().manual_ids==manual_selection.ids,"Actual run must retain user origin without a model load acknowledgement");store.replace_session_skills("manual-user",{manual_workspace.identity(),{},{}},selected.revision).get();manual_settings.provider.endpoint=base+"/manual-cleared";AgentRunner cleared_runner(store,manual_settings);cleared_runner.start("manual-cleared","manual-user","Continue with no attached guide");require(cleared_runner.execute("manual-cleared").state==RunState::completed&&store.run_skills("manual-cleared").get().ids.empty(),"User removal must retire active guidance before the next actual provider boundary");
        rejects<Conflict>([&]{runner.execute("main");});
        require(store.history("main").get().size()==4,"Duplicate execute must not damage completed transcript");
        store.create_session("claim","Concurrent claim").get();auto claimed=settings;
        claimed.provider.endpoint=base+"/claim";AgentRunner claimant(store,claimed);claimant.start("claim","claim","Concurrent claim protocol case");
        std::stop_source claim_stop;
        std::jthread fallback_cancel([&]{std::this_thread::sleep_for(1s);claim_stop.request_stop();});
        auto owner=std::async(std::launch::async,[&]{return claimant.execute("claim",claim_stop.get_token());});
        bool progressed=false;
        for(int attempt=0;attempt<100 && !progressed;++attempt) {
            for(const auto& event:store.events("claim").get()) if(event.kind=="model.text") progressed=true;
            if(!progressed) std::this_thread::sleep_for(5ms);
        }
        require(progressed,"Owner must enter actual network stream before the competing claim");
        rejects<Conflict>([&]{claimant.execute("claim");});
        require(store.run("claim").get().state==RunState::running,"Losing claim must not fail the active owner");
        claim_stop.request_stop();require(owner.get().state==RunState::cancelled,"Owner must observe its actual cancellation");
        for(const auto* route:{"error","denied","delay","limit"}) {
            store.create_session(route,route).get();auto variant=settings;
            variant.provider.endpoint=base+"/"+route;if(std::string(route)=="limit") variant.max_turns=1;
            AgentRunner action(store,variant);action.start(route,route,std::string("Protocol case ")+route);
            const auto final=std::string(route)=="delay"?cancel_after_stream(action,store,route):action.execute(route);
            require(final.state==(std::string(route)=="delay"?RunState::cancelled:(std::string(route)=="denied"?RunState::completed:RunState::failed)),"Terminal state must reflect actual outcome");
            if(std::string(route)=="error") require(Json::parse(store.events(route).get().back().json)["status"]==429,"HTTP failure must be recorded without provider body");
            if(std::string(route)=="denied") {
                const auto rows=store.history(route).get();const auto denied=Json::parse(Json::parse(rows[2].json).at("content").get<std::string>());
                require(denied["error"]["code"]=="access_denied","Outside workspace access must become an actual denied result");
            }
        }
        store.create_session("deadline","Deadline").get();auto timed=settings;
        timed.provider.endpoint=base+"/delay";timed.run_timeout=5s;
        AgentRunner deadline(store,timed);deadline.start("deadline","deadline","Protocol deadline case");
        require(deadline.execute("deadline").state==RunState::failed,"Run deadline must fail rather than simulate a response");
        require(Json::parse(store.events("deadline").get().back().json)["reason"]=="agent_timeout","Run deadline must have its own reason");
        require(streamed(store,"deadline"),"Deadline fixture must expire after actual provider stream entry");
        store.create_session("atomic","Atomic lifecycle").get();
        rejects<DatabaseError>([&]{store.start_prompt_run("invalid","atomic","invalid JSON").get();});
        require(store.history("atomic").get().empty(),"Failed start must not leave a prompt");
        rejects<NotFound>([&]{store.run("invalid").get();});
        store.start_prompt_run("atomic","atomic",R"({"content":"Prompt"})").get();
        rejects<Conflict>([&]{store.append_user_message("atomic",R"({"content":"racing"})").get();});
        store.transition("atomic",RunState::queued,RunState::running).get();
        XlangSqlite faults(argv[1],roots);
        faults.execute("CREATE TRIGGER reject_tool BEFORE INSERT ON messages WHEN NEW.role='tool' BEGIN SELECT RAISE(ABORT,'fault'); END");
        rejects<DatabaseError>([&]{store.record_tool_turn("atomic",R"({"content":"","tool_calls":[]})",{R"({"content":"Result"})"}).get();});
        require(store.history("atomic").get().size()==1,"Tool batch fault must not leave dangling assistant calls");
        faults.execute("DROP TRIGGER reject_tool");
        faults.execute("CREATE TRIGGER reject_completion BEFORE INSERT ON events WHEN NEW.kind='run.completed' BEGIN SELECT RAISE(ABORT,'fault'); END");
        rejects<DatabaseError>([&]{store.complete_run("atomic",R"({"content":"Answer"})").get();});
        require(store.run("atomic").get().state==RunState::running && store.history("atomic").get().size()==1,"Final answer/state must roll back together");
        faults.execute("DROP TRIGGER reject_completion");store.complete_run("atomic",R"({"content":"Answer"})").get();
        store.create_session("blocker-session","Uncertain process effect fixture").get();
        store.start_prompt_run("blocker-run","blocker-session",R"({"content":"Synthetic uncertain process receipt"})").get();
        store.transition("blocker-run",RunState::queued,RunState::running).get();
        OperationSpec blocker_spec{"blocker-run",WorkspaceTools(argv[4]).identity(),"run_process",R"({"background":true,"persistent":false})",{"process-profile:powershell"}};
        const auto blocker_expiry=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+60000;
        store.request_operation("blocker-operation",blocker_spec,blocker_expiry).get();
        store.decide_operation("blocker-operation",OperationDecision::allow,"fixture-controller").get();
        store.claim_operation("blocker-operation",blocker_spec).get();
        store.finish_operation("blocker-operation",OperationState::uncertain,R"({"reason":"fixture_quarantine"})").get();
        store.transition("blocker-run",RunState::running,RunState::failed,R"({"reason":"fixture_quarantine"})").get();
        auto quarantined=settings;quarantined.provider.endpoint=base+"/blocked-effect";quarantined.approved_edits=true;
        store.create_session("blocked-effect","Quarantined workspace edit reporting").get();
        AgentRunner blocked_runner(store,quarantined);blocked_runner.start("blocked-effect","blocked-effect","Please make the requested workspace edit");
        auto blocked_run=std::async(std::launch::async,[&]{return blocked_runner.execute("blocked-effect");});std::optional<Operation> blocked_proposal;
        const auto blocked_deadline=std::chrono::steady_clock::now()+5s;
        while(std::chrono::steady_clock::now()<blocked_deadline&&!blocked_proposal){
            if(blocked_run.wait_for(0ms)==std::future_status::ready)throw std::runtime_error("Quarantined edit run ended before approval: "+to_string(blocked_run.get().state));
            for(const auto& operation:store.operations("blocked-effect").get())if(operation.state==OperationState::awaiting_approval){blocked_proposal=operation;break;}
            if(!blocked_proposal)std::this_thread::sleep_for(5ms);
        }
        require(blocked_proposal.has_value(),"Quarantined edit must still use the normal controller approval");
        store.decide_operation(blocked_proposal->id,OperationDecision::allow,"fixture-controller").get();
        require(blocked_run.get().state==RunState::completed,"An uncertain prior effect must reach the model as a tool result instead of failing opaquely");
        const auto blocked_history=store.history("blocked-effect").get();require(blocked_history.size()==4,"The blocked effect must remain in the ordinary persisted tool loop");
        const auto blocked_result=Json::parse(Json::parse(blocked_history[2].json).at("content").get<std::string>());
        require(blocked_result.at("error").at("code")=="effect_resource_uncertain"&&blocked_result.at("error").at("message").get<std::string>().find("blocker-operation")!=std::string::npos,"The model-visible error must identify the unresolved effect without authorizing a write");
        const auto blocked_operations=store.operations("blocked-effect").get();
        require(store.operation("blocker-operation").get().state==OperationState::uncertain&&blocked_operations.size()==1&&blocked_operations[0].state==OperationState::cancelled,"The earlier effect must remain quarantined and the blocked edit proposal must be cancelled");
        require(WorkspaceTools(argv[4]).read_file("README.md").content=="Actual file content written by fixture\n","A blocked approved edit must not alter workspace bytes");
        store.close();
        {Repository reopened(argv[1],roots);require(reopened.run("main").state==RunState::completed && reopened.history("main").size()==4,"Agent state/conversation must survive reopen");}
        std::cout<<"Native agent loop/atomicity contracts passed using a synthetic inference peer and real filesystem tools; no live model was called\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
