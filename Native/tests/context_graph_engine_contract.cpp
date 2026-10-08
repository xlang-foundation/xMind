// Synthetic provider/count/compaction responses are supplied by the independent
// Node peer. Graph ownership, transports, native reads and xlang3 SQLite are real.
#include "agentflow/graph_service.hpp"
#include "agentflow/context_manager.hpp"
#include "agentflow/http_server.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <thread>
#include <iostream>
#include <cstdlib>
#include <algorithm>

using namespace agentflow;
using Json=nlohmann::json;
using namespace std::chrono_literals;
namespace {
const char* stage="entry";
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class Error,class Action>void rejects(Action action,const char* reason){try{action();}catch(const Error&){return;}throw std::runtime_error(reason);}
template<class Condition>void eventually(Condition condition,const char* reason){const auto until=std::chrono::steady_clock::now()+15s;while(!condition()){if(std::chrono::steady_clock::now()>=until)throw std::runtime_error(reason);std::this_thread::sleep_for(5ms);}}
std::string provider_identity(const std::string& model){return Json{{"wire","responses"},{"model_id",model}}.dump();}
AgentSettings settings(const std::string& workspace,const std::string& endpoint){
    AgentSettings result;result.workspace=workspace;result.provider.model="graph-context-left";result.selectable_models={"graph-context-right"};
    result.provider.endpoint=endpoint;result.provider.wire=ProviderWire::responses;result.provider.tools=Capability::supported;
    result.provider.stream_usage=Capability::supported;result.provider.output_limit=Capability::supported;result.provider.deadline=10s;result.provider.idle_timeout=10s;
    result.credential=CredentialReference{"provider","graph-fixture","model-api"};result.max_turns=5;result.max_output_tokens=32;result.run_timeout=20s;
    ContextRuntimePolicy runtime;runtime.buffer_tokens=100;runtime.compaction.retain_recent_groups=1;
    for(const auto* model:{"graph-context-left","graph-context-right"}){
        VerifiedContextCapacity capacity;capacity.provider_identity_json=provider_identity(model);capacity.wire="responses";capacity.model_id=model;
        auto selected=result.provider;selected.model=model;capacity.route_identity=context_route_identity(selected,capacity.provider_identity_json);
        capacity.revision=1;capacity.source_binding=context_digest(std::string("Synthetic explicitly registered fixture capacity ")+model);capacity.verified=true;
        capacity.input_tokens=1000;capacity.output_tokens=32;capacity.default_output_tokens=32;runtime.model_capacities.emplace(model,std::move(capacity));
    }
    result.context=std::move(runtime);return result;
}
void credential(PersistenceService& store){const std::string key="context-graph-fixture-token-not-a-real-key";
    store.put_credential("provider","graph-fixture","model-api","Synthetic disposable provider credential only",SecretBytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(key.data()),key.size())),0).get();
}
const std::string inspect=R"({"nodes":[{"id":"left","type":"agent","prompt":"Investigate graph-context-left using actual repeated native file reads.","model_id":"graph-context-left","depends_on":[]},{"id":"right","type":"agent","prompt":"Investigate graph-context-right using actual repeated native file reads.","model_id":"graph-context-right","depends_on":[]}]})";
void catalog(PersistenceService& store){
    auto human=Json::parse(inspect);human["nodes"].push_back({{"id","review"},{"type","human"},{"prompt","Confirm the observed investigation."},{"depends_on",Json::array({"left","right"})}});
    auto excessive=Json::object();excessive["nodes"]=Json::array();for(int i=0;i<9;++i)excessive["nodes"].push_back({{"id","agent_"+std::to_string(i)},{"type","agent"},{"prompt","Synthetic excess admission."},{"depends_on",Json::array()}});
    GraphCatalogStore(store).apply(Json{{"graphs",Json::array({{{"id","human"},{"spec",human}},{{"id","inspect"},{"spec",Json::parse(inspect)}},{{"id","excessive"},{"spec",excessive}}})}}.dump());
}
void marker(const std::filesystem::path& root,const char* name){eventually([&]{return std::filesystem::is_regular_file(root/name);},"Actual independent peer requests did not reach the wire");}
std::int64_t scalar(XlangSqlite& sql,const std::string& query,const std::vector<SqlValue>& args){const auto rows=sql.execute(query,args).rows;require(rows.size()==1&&rows[0].size()==1,"Expected actual SQL scalar");return std::get<std::int64_t>(rows[0][0]);}
const std::string access_token="synthetic-context-graph-access-token-not-live";
struct Serving{HttpServer& server;std::thread worker;explicit Serving(HttpServer& value):server(value),worker([&value]{value.listen();}){}~Serving(){server.stop();worker.join();}};
struct Http{
    httplib::Client client;httplib::Headers bearer{{"Authorization","Bearer "+access_token}};
    explicit Http(int port):client("127.0.0.1",port){client.set_connection_timeout(2,0);client.set_read_timeout(5,0);client.set_follow_location(false);eventually([&]{return static_cast<bool>(client.Get("/v1/health",bearer));},"Actual graph HTTP listener did not become ready");}
    Json get(const std::string& path,int status=200){const auto result=client.Get(path,bearer);require(result&&result->status==status,"Unexpected actual graph HTTP read status");return Json::parse(result->body);}
    Json post(const std::string& path,const Json& input,int status=202,const httplib::Headers* authorization=nullptr){const auto result=client.Post(path,authorization?*authorization:bearer,input.dump(),"application/json");require(result&&result->status==status,"Unexpected actual graph HTTP mutation status");return Json::parse(result->body);}
    std::optional<Json> observe(const std::string& id){
        const auto result=client.Get("/v1/graph-runs/"+id,bearer);require(static_cast<bool>(result),"Actual graph HTTP observation did not respond");const auto value=Json::parse(result->body);
        if(result->status==409){require(value==Json{{"detail","Graph context observation changed"}},"Only a typed changing-context observation may be retried");return {};}
        require(result->status==200,"Actual graph observation returned an unexpected status");return value;
    }
};
void public_graph_context(const Json& value,const std::string& id,const std::string& session,const std::string& state,bool resumable,std::optional<std::int64_t> remaining={}){
    require(value.at("run").at("id")==id&&value.at("run").at("session_id")==session&&value.at("run").at("state")==state&&value.at("run").at("graph_root")==true,"Graph HTTP projection must retain its actual owner and state");
    const auto& context=value.at("context");require(context.is_object()&&context.size()==3&&context.contains("enabled")&&context.contains("resumable")&&context.contains("remaining_active_ms"),"Public graph context must remain a fixed nonprivate projection");
    require(context["enabled"]==true&&context["resumable"]==resumable,"Graph resume capability must reflect actual current owner eligibility");
    if(remaining)require(context["remaining_active_ms"]==*remaining,"Closed graph clock must reflect the captured actual remaining budget");else require(context["remaining_active_ms"].is_null(),"Open graph segment must not advertise a fabricated remaining clock");
}
void exercise(const std::filesystem::path& root,const std::vector<std::string>& imports,const std::string& base){
    const auto database=(root/"graph-context.sqlite").string();PersistenceService store(database,imports);credential(store);catalog(store);
    const auto frozen=settings(root.string(),base+"/main/responses");store.create_session("main","Actual contextual registered graph").get();store.create_session("excess","Rejected bounded graph admission").get();
    GraphRootRecord paused;GraphContextOwnerRecord closed;RootBudgetRecord budget;
    stage="parallel_and_compaction";
    {
        GraphService owner(store,frozen,1,8,2);
        HttpServer server(store,access_token,nullptr,nullptr,{},{},{},nullptr,&owner);const auto port=server.bind(0);require(port>0,"Actual graph HTTP listener did not bind");Serving serving(server);Http http(port);
        rejects<GraphContextUnavailable>([&]{owner.submit_graph("excess-root","excess","excessive",1,"Reject excess contextual Agents.");},"Nine agent nodes cannot acquire another root allowance");
        rejects<NotFound>([&]{store.run("excess-root").get();},"Rejected admission must not create a root");require(store.history("excess").get().empty(),"Rejected graph admission must not append a user");
        owner.submit_graph("main-root","main","human",1,"Synthetic root coordination.");eventually([&]{const auto observed=http.observe("main-root");return observed&&observed->at("run").at("state")=="paused";},"Observed graph children did not reach the human pause through the actual HTTP read path");
        paused=store.graph_run("main-root").get();closed=store.graph_context_owner("main-root").get();budget=store.root_budget("main-root").get();
        require(closed.segment.has_value(),"Paused graph requires its actual closed budget segment");
        const auto public_paused=http.get("/v1/graph-runs/main-root");public_graph_context(public_paused,"main-root","main","paused",false,closed.segment->remaining_active_ms);require(public_paused["checkpoint_revision"]==paused.checkpoint_revision&&public_paused["checkpoint"]==Json::parse(paused.checkpoint_json),"Paused HTTP observation must preserve the exact actual checkpoint");
        const auto before_rejected_resume=store.root_budget("main-root").get();
        http.post("/v1/graph-runs/main-root/resume",{{"expected_checkpoint_revision",paused.checkpoint_revision}},409);
        require(store.graph_run("main-root").get().checkpoint_revision==paused.checkpoint_revision&&store.root_budget("main-root").get().revision==before_rejected_resume.revision,"Waiting human resume rejection cannot reopen or change budget/checkpoint metadata");
        const auto waiting=owner.graph_context("main-root");require(waiting.enabled&&!waiting.resumable&&closed.segment&&waiting.remaining_active_ms==closed.segment->remaining_active_ms,"Actual waiting human owner cannot advertise ready resume");
        require(closed.segment&&closed.segment->state=="closed"&&closed.segment->active_elapsed_ms>0&&closed.segment->remaining_active_ms>0&&closed.segment->remaining_active_ms<budget.spec.wall_limit_ms,"Human pause must own a measured closed clock, not a reset timeout");
        require(budget.spec.policy_id=="native.graph-context"&&budget.spec.max_children==8&&budget.spec.max_parallel==2&&budget.spec.max_model_calls==32&&budget.children_admitted==2&&budget.model_calls_reserved==12&&budget.parent_model_calls_reserved==0&&budget.parent_calls_held==0,"Both model owners share actual10 inference+2 maintenance physical funding with no parent call");
        const auto children=store.children("main-root").get();require(children.size()==2&&store.history("main").get().size()==1,"Child contexts must remain isolated from root visible history");
        for(const auto& child:children){
            const auto admission=store.child_admission(child.id).get();const auto captured=std::find_if(closed.authority.agents.begin(),closed.authority.agents.end(),[&](const auto& a){return a.node_id==child.node_id;});
            require(captured!=closed.authority.agents.end()&&captured->turn_limit==5&&admission.backend_identity==captured->backend_identity,"Actual graph child must retain its configured model and five-turn authority");
            const auto history=store.run_history(child.id).get();require(child.state==RunState::completed&&history.size()==10,"Compaction must not remove original child tool groups or final response");std::int64_t responses=0;
            for(const auto& item:history)if(item.role=="assistant"){
                ++responses;const auto actual=Json::parse(item.json);const auto input=20+responses+(child.node_id=="right"?30:0);
                require(actual["model"]==captured->model_id&&actual["usage"]["input_tokens"]==input&&actual["usage"]["output_tokens"]==5&&actual["usage"]["total_tokens"]==input+5&&actual.contains("provider_items"),"Actual per-response supplied usage and private reasoning must remain owned by that child");
            }require(responses==5,"Registered graph logical turns must not silently clamp to depth-one leaf policy");
            ContextBinding binding;binding.provider_identity_json=captured->provider_identity_json;binding.authority_identity=captured->backend_identity;
            const auto projection=store.context_projection({ContextScopeKind::execution,child.id},binding).get();
            require(projection&&projection->head.revision==1&&projection->projection_json.find("1.00000000000000000001")!=std::string::npos&&projection->actual_usage_json==R"({"input_tokens":41,"output_tokens":9,"total_tokens":50})","Each child needs its own exact acknowledged canonical projection and actual maintenance usage");
        }
        owner.close();require(store.run("main-root").get().state==RunState::paused,"Owner shutdown must preserve the clean contextual pause");
    }
    stage="stale_authority_and_cas";
    {auto changed=frozen;changed.instructions+=" Synthetic changed backend instructions.";GraphService stale(store,changed,1,8,2);
        require(stale.graph_context("main-root").enabled&&!stale.graph_context("main-root").resumable,"Changed native authority remains inspectable without granting resume");
        rejects<GraphContextUnavailable>([&]{stale.human_input("main-root","review",R"({"answer":"Continue"})","fixture-controller",paused.checkpoint_revision);},"A changed owner cannot commit human input before rebinding its captured authority");
        require(store.graph_run("main-root").get().checkpoint_revision==paused.checkpoint_revision&&store.root_budget("main-root").get().model_calls_reserved==12,"Stale metadata must not alter answer, budget or provider dispatch");stale.close();}
    {auto changed=frozen;changed.selectable_models.clear();GraphService stale(store,changed,1,8,2);
        require(!stale.idle(),"A paused graph with a removed model override remains owned for inspection and cancellation");
        rejects<std::invalid_argument>([&]{stale.human_input("main-root","review",R"({"answer":"Continue"})","fixture-controller",paused.checkpoint_revision);},"Removed model configuration cannot commit human input");
        require(store.graph_run("main-root").get().checkpoint_revision==paused.checkpoint_revision&&store.root_budget("main-root").get().model_calls_reserved==12,"Model drift adoption must not replay or change captured metadata");stale.close();}
    std::this_thread::sleep_for(150ms);require(store.graph_context_owner("main-root").get().segment->remaining_active_ms==closed.segment->remaining_active_ms,"Paused wall time must not consume or recreate active allowance");
    stage="answered_pause_and_cancel";
    {
        GraphService owner(store,frozen,1,8,2);HttpServer server(store,access_token,nullptr,nullptr,{},{},{},nullptr,&owner);const auto port=server.bind(0);require(port>0,"Actual held graph HTTP listener did not bind");Serving serving(server);Http http(port);
        store.create_session("cancel","Actual held graph cancellation").get();owner.submit_graph("cancel-root","cancel","inspect",1,"Synthetic cancel-root held transport.");marker(root,"cancel-started");
        public_graph_context(http.get("/v1/graph-runs/cancel-root"),"cancel-root","cancel","running",false);const auto actual_children=http.get("/v1/graph-runs/cancel-root/children");require(actual_children.is_array()&&actual_children.size()==2,"Actual executing HTTP graph must expose both admitted concurrent children");for(const auto& child:actual_children)require(child["parent_id"]=="cancel-root"&&child["state"]=="running","Held native child transports must remain actually running");
        rejects<Conflict>([&]{owner.human_input("main-root","review",R"({"answer":"Continue"})","fixture-controller",paused.checkpoint_revision+1);},"Input CAS must reject a stale displayed checkpoint");
        owner.human_input("main-root","review",R"({"answer":"Continue after actual inspection"})","fixture-controller",paused.checkpoint_revision);
        owner.close();require(store.run("cancel-root").get().state==RunState::cancelled,"Shutdown must drain actual held child transports");
        const auto answered=store.graph_run("main-root").get();require(answered.run.state==RunState::paused&&GraphCoordinator(GraphPlan(answered.specification_json),answered.checkpoint_json,GraphRestoreMode::live).inspect().finished,"Queued final human answer must remain a clean closed owner on shutdown");
        require(store.root_budget("main-root").get().model_calls_reserved==12&&store.graph_context_owner("main-root").get().segment->remaining_active_ms==closed.segment->remaining_active_ms,"Queued wake must not open a segment or make a hidden provider call");
    }
    stage="reopen_and_explicit_join";store.close();
    {
        PersistenceService reopened(database,imports);GraphService owner(reopened,frozen,1,8,2);const auto answered=reopened.graph_run("main-root").get();
        require(answered.run.state==RunState::paused&&!owner.idle(),"A ready clean pause is adopted without automatic replay");
        require(owner.graph_context("main-root").resumable,"Final human-only frontier advertises resume from actual backend eligibility");
        {
            HttpServer server(reopened,access_token,nullptr,nullptr,{},{},{},nullptr,&owner);const auto port=server.bind(0);require(port>0,"Actual resumed graph HTTP listener did not bind");Serving serving(server);Http http(port);
            const auto ready=http.get("/v1/graph-runs/main-root");public_graph_context(ready,"main-root","main","paused",true,closed.segment->remaining_active_ms);require(ready["checkpoint_revision"]==answered.checkpoint_revision,"Explicit resume must use the observed current checkpoint");
            const auto before=reopened.root_budget("main-root").get();httplib::Headers unauthenticated;
            http.post("/v1/graph-runs/main-root/resume",{{"expected_checkpoint_revision",answered.checkpoint_revision}},401,&unauthenticated);
            http.post("/v1/graph-runs/main-root/resume",{{"expected_checkpoint_revision",answered.checkpoint_revision},{"actor","client-selected-owner"}},400);
            http.post("/v1/graph-runs/main-root/resume",{{"expected_checkpoint_revision",answered.checkpoint_revision+1}},409);
            require(reopened.run("main-root").get().state==RunState::paused&&reopened.root_budget("main-root").get().revision==before.revision&&reopened.graph_context_owner("main-root").get().segment->ordinal==1,"Rejected authentication/schema/CAS resume attempts cannot open a clock or consume model allowance");
            const auto accepted=http.post("/v1/graph-runs/main-root/resume",{{"expected_checkpoint_revision",answered.checkpoint_revision}});require(accepted["id"]=="main-root"&&accepted["session_id"]=="main","Authenticated resume must queue its existing actual owner");
            eventually([&]{const auto observed=http.observe("main-root");return observed&&observed->at("run").at("state")=="completed";},"Final human-only answer did not wake its same owner join through actual HTTP resume");
            const auto final_http=http.get("/v1/graph-runs/main-root");const auto final_clock=reopened.graph_context_owner("main-root").get();require(final_clock.segment.has_value(),"Completed graph requires its actual closed budget segment");public_graph_context(final_http,"main-root","main","completed",false,final_clock.segment->remaining_active_ms);require(final_http["checkpoint_revision"]==reopened.graph_run("main-root").get().checkpoint_revision,"Completed HTTP graph must carry its settled checkpoint");
        }
        const auto final=reopened.graph_context_owner("main-root").get();require(final.segment&&final.segment->ordinal==2&&final.segment->state=="closed"&&final.segment->remaining_active_ms<=closed.segment->remaining_active_ms,"Resume consumes the old remaining clock and closes exactly one new active segment");
        require(reopened.root_budget("main-root").get().model_calls_reserved==12&&reopened.children("main-root").get().size()==2,"Final join must not replay or invent another provider/child allowance");
        const auto history=reopened.history("main").get();require(history.size()==2&&Json::parse(history.back().json)["source"]=="graph_join"&&!Json::parse(history.back().json).contains("usage"),"Root join retains original history without invented aggregate response metrics");
        owner.close();XlangSqlite sql(database,imports);
        require(scalar(sql,"SELECT count(*) FROM agent_model_call_reservations WHERE root_run_id=? AND state='finished'",{"main-root"})==12&&scalar(sql,"SELECT count(*) FROM inference_steps WHERE root_run_id=?",{"main-root"})==10,"Actual durable physical and logical memberships must match observed transport attempts");
        require(scalar(sql,"SELECT count(*) FROM context_measures WHERE owner_id IN (SELECT id FROM runs WHERE parent_run_id=?) AND state='finished'",{"main-root"})==12,"Provider input counts remain actual independent ledger observations");
        require(scalar(sql,"SELECT sum(active_elapsed_ms)+min(remaining_active_ms) FROM agent_budget_segments WHERE root_run_id=?",{"main-root"})==20000,"Segment arithmetic must conserve the exact admitted wall allowance");reopened.close();
    }
}
void crash(const std::filesystem::path& root,const std::vector<std::string>& imports,const std::string& base){
    PersistenceService store((root/"recovery.sqlite").string(),imports);credential(store);catalog(store);store.create_session("recover","Actual interrupted graph owner").get();
    GraphService owner(store,settings(root.string(),base+"/recover/responses"),1,8,2);owner.submit_graph("recover-root","recover","inspect",1,"Synthetic recovery transport.");marker(root,"recover-started");
    require(store.root_budget("recover-root").get().model_calls_reserved==2&&store.children("recover-root").get().size()==2,"Crash boundary requires two actual admitted started requests");std::_Exit(0);
}
void reopen(const std::filesystem::path& root,const std::vector<std::string>& imports,const std::string& base){
    PersistenceService store((root/"recovery.sqlite").string(),imports);GraphService owner(store,settings(root.string(),base+"/recover/responses"),1,8,2);
    require(store.run("recover-root").get().state==RunState::failed&&owner.idle()&&store.root_budget("recover-root").get().model_calls_reserved==2,"Recovery must interrupt physical ownership without replay/reset");
    for(const auto& child:store.children("recover-root").get())require(child.state==RunState::failed&&store.run_history(child.id).get().size()==1,"Interrupted child keeps its original objective without a fabricated response");
    rejects<NotFound>([&]{owner.resume_graph("recover-root","fixture-controller",store.graph_run("recover-root").get().checkpoint_revision);},"Interrupted roots cannot acquire a new wake");
    XlangSqlite sql((root/"recovery.sqlite").string(),imports);require(scalar(sql,"SELECT count(*) FROM agent_model_call_reservations WHERE root_run_id=? AND state='interrupted'",{"recover-root"})==2,"Unfinished actual members remain interrupted");owner.close();store.close();
}
}
int main(int argc,char** argv)try{
    if(argc!=6)throw std::invalid_argument("Expected owned fixture, modules, stdlib, loopback endpoint and mode");const std::filesystem::path root=std::filesystem::u8path(argv[1]);const std::vector<std::string> imports{argv[2],argv[3]};const std::string mode=argv[5];
    if(mode=="exercise")exercise(root,imports,argv[4]);else if(mode=="crash")crash(root,imports,argv[4]);else if(mode=="reopen")reopen(root,imports,argv[4]);else throw std::invalid_argument("Unknown fixture mode");
    std::cout<<"Native contextual registered graph boundary passed; synthetic Responses/count/compact replies and credential only, actual native transports/files/xlang3 SQLite.\n";return 0;
}catch(const DatabaseError&){std::cerr<<"context_graph_engine_contract_failed stage="<<stage<<" class=database\n";return 1;}
 catch(const std::exception&){std::cerr<<"context_graph_engine_contract_failed stage="<<stage<<" class=contract\n";return 1;}
