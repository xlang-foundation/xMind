#include "agentflow/agent_runner.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
#include <thread>
#include <condition_variable>
#include <mutex>

using namespace agentflow;
using Json=nlohmann::json;
using namespace std::chrono_literals;
namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class Error,class Function> void rejects(Function action) {
    try {action();} catch(const Error&) {return;} throw std::runtime_error("Expected rejection did not occur");
}
bool streamed(PersistenceService& store,const std::string& id){
    for(const auto& event:store.events(id).get())if(event.kind=="model.text")return true;
    return false;
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
    if(argc!=6) return 2;
    try {
        const std::vector<std::string> roots{argv[2],argv[3]};
        PersistenceService store(argv[1],roots);
        const std::string base=argv[5],synthetic="engine-protocol-test-not-a-real-key";
        SecretBytes secret({reinterpret_cast<const std::uint8_t*>(synthetic.data()),synthetic.size()});
        store.put_credential("test","provider","provider:chat","Synthetic test credential",std::move(secret),0).get();
        AgentSettings settings;settings.provider={base+"/main","fixture-deployment",Capability::supported,Capability::supported};
        settings.workspace=argv[4];settings.credential=CredentialReference{"test","provider","provider:chat"};
        store.create_session("main","Actual filesystem protocol fixture").get();
        AgentRunner runner(store,settings);runner.start("main","main","Read README");
        require(runner.execute("main").state==RunState::completed,"Actual provider/tool loop must complete its protocol turn");
        const auto history=store.history("main").get();
        require(history.size()==4 && history[1].role=="assistant" && history[2].role=="tool" && history[3].role=="assistant","Actual tool conversation must be persisted");
        const auto tool=Json::parse(Json::parse(history[2].json).at("content").get<std::string>());
        require(tool["content"]=="Actual file content written by fixture\n","Actual filesystem result must reach durable conversation");
        bool started=false,completed=false;
        for(const auto& event:store.events("main").get()) {if(event.kind=="tool.started") started=true;if(event.kind=="tool.completed") completed=true;}
        require(started && completed,"Actual tool execution events must persist");
        store.create_session("skill","Native skill execution fixture").get();auto skilled=settings;skilled.provider.endpoint=base+"/skill";AgentRunner skill_runner(store,skilled);skill_runner.start("skill","skill","Load the synthetic workspace skill and inspect its actual companion file");
        require(skill_runner.execute("skill").state==RunState::completed,"Native skill loading must complete its actual model/tool loop");const auto skill_history=store.history("skill").get();require(skill_history.size()==7,"Actual skill turn and same-batch deferral must persist as one matched conversation");
        const auto blocked=Json::parse(Json::parse(skill_history[3].json).at("content").get<std::string>());require(blocked.at("error").at("code")=="repository_instructions_required","Companion read in the activation batch must wait for delivered skill guidance");
        const auto companion=Json::parse(Json::parse(skill_history[5].json).at("content").get<std::string>());require(companion.at("content")=="Actual native skill companion bytes\n","Delivered skill must use actual authorized native file reads");bool skill_binding=false;for(const auto& event:store.events("skill").get())if(event.kind=="agent.repository_scope"&&Json::parse(event.json).contains("skills"))skill_binding=true;require(skill_binding,"Actual run must retain Native skill snapshot metadata");
        store.create_session("skill-capacity","Native instruction capacity fixture").get();auto capacity=settings;capacity.provider.endpoint=base+"/skill-capacity";capacity.instructions="Synthetic budget guard fixture "+std::string(53000,'x');AgentRunner capacity_runner(store,capacity);capacity_runner.start("skill-capacity","skill-capacity","Synthetic request to load a guide beyond the remaining instruction budget");require(capacity_runner.execute("skill-capacity").state==RunState::completed,"An oversized guide load must return a tool rejection and allow the actual agent loop to continue");const auto capacity_history=store.history("skill-capacity").get();require(capacity_history.size()==4,"Rejected activation must have a complete matched durable tool turn");const auto capacity_error=Json::parse(Json::parse(capacity_history[2].json).at("content").get<std::string>());require(capacity_error.at("error").at("code")=="file_unavailable","Remaining native instruction capacity must be checked before acknowledging guide activation");for(const auto& event:store.events("skill-capacity").get())if(event.kind=="agent.repository_scope")require(!Json::parse(event.json).contains("skills"),"Rejected activation must never become delivered skill metadata");
        auto followed=skilled;followed.provider.endpoint=base+"/skill-follow-up";AgentRunner following(store,followed);following.start("skill-follow-up","skill","Continue using the previously activated native skill");require(following.execute("skill-follow-up").state==RunState::completed,"A continued prompt must restore skill guidance before its first provider request");require(store.run_skills("skill-follow-up").get().ids==std::vector<std::string>{"inspect"},"Actual continued agent must retain its committed native selection");
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
        store.close();
        {Repository reopened(argv[1],roots);require(reopened.run("main").state==RunState::completed && reopened.history("main").size()==4,"Agent state/conversation must survive reopen");}
        std::cout<<"Native agent loop/atomicity contracts passed using a synthetic inference peer and real filesystem tools; no live model was called\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
