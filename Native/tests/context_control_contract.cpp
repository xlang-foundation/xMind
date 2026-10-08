// Real AgentService/HttpServer/ContextManager/xlang3 SQLite controllers.
// Only provider replies, opaque data, objectives and credentials are synthetic.
// No run/compaction lifecycle is advanced through fixture state setters.
#include "agentflow/agent_service.hpp"
#include "agentflow/http_server.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

using namespace agentflow;
using namespace std::chrono_literals;
namespace {
using Json=nlohmann::json;
const char* stage="entry";
const std::string key="synthetic-context-control-provider-key-not-live";
const std::string token="synthetic-context-control-access-token-32-bytes";
const std::string model_a="synthetic-control-a",model_b="synthetic-control-b";
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class F>void eventually(F action,const char* reason){const auto until=std::chrono::steady_clock::now()+18s;while(std::chrono::steady_clock::now()<until){if(action())return;std::this_thread::sleep_for(5ms);}throw std::runtime_error(reason);}
template<class E,class F>void rejects(F action,const char* reason){try{action();}catch(const E&){return;}throw std::runtime_error(reason);}
void flag(const std::filesystem::path& root,const std::string& name){std::ofstream out(root/name,std::ios::binary);out<<"Synthetic provider barrier released by real controller fixture.\n";require(static_cast<bool>(out),"Owned provider barrier write failed");}
void marker(const std::filesystem::path& root,const std::string& name){eventually([&]{return std::filesystem::is_regular_file(root/name);},"Synthetic provider did not observe the actual transport boundary");}
std::size_t count(const std::filesystem::path& root,const std::string& kind,const std::string& scope="owned",const std::string& model=model_a){std::ifstream in(root/"peer-audit.jsonl");std::string line;std::size_t value=0;while(std::getline(in,line)){const auto item=Json::parse(line);if(item["kind"]==kind&&item["scope"]==scope&&item["model"]==model)++value;}return value;}
void exact_history(const std::vector<Message>& before,const std::vector<Message>& after){require(before.size()==after.size(),"Context control cannot create/remove transcript messages");for(std::size_t i=0;i<before.size();++i)require(before[i].sequence==after[i].sequence&&before[i].role==after[i].role&&before[i].json==after[i].json,"Context control must preserve exact original private receipts and metrics");}
AgentSettings settings(const std::string& base,const std::string& mode){AgentSettings value;
    value.provider.endpoint=base+"/"+mode+"/responses";value.provider.model=model_a;value.provider.wire=ProviderWire::responses;
    value.provider.tools=Capability::unsupported;value.provider.stream_usage=Capability::supported;value.provider.output_limit=Capability::supported;
    value.provider.deadline=10s;value.provider.idle_timeout=10s;value.selectable_models={model_b};value.max_turns=3;value.max_output_tokens=64;value.run_timeout=25s;
    value.instructions="Explicit synthetic native context-control fixture. Original user objectives and native authority remain exact.";
    value.credential=CredentialReference{"fixture","context-control","provider:synthetic-context-control"};
    ContextRuntimePolicy policy;policy.compaction.automatic=false;policy.compaction.retain_recent_groups=1;policy.compaction.maintenance_deadline_ms=10000;
    // No invented model capacity: manual maintenance is explicit. Seed roots
    // cannot dispatch automatic counts or compaction from a guessed window.
    value.context=std::move(policy);return value;
}
void put_key(PersistenceService& store){store.put_credential("fixture","context-control","provider:synthetic-context-control","Synthetic disposable control fixture key",SecretBytes({reinterpret_cast<const std::uint8_t*>(key.data()),key.size()}),0).get();}
struct Serving{HttpServer& server;std::thread worker;explicit Serving(HttpServer& value):server(value),worker([&value]{value.listen();}){}~Serving(){server.stop();worker.join();}};
struct Http {
    httplib::Client client;httplib::Headers bearer{{"Authorization","Bearer "+token}};
    explicit Http(int port):client("127.0.0.1",port){client.set_connection_timeout(2,0);client.set_read_timeout(5,0);client.set_follow_location(false);eventually([&]{return static_cast<bool>(client.Get("/v1/health",bearer));},"Real HTTP listener did not become ready");}
    Json get(const std::string& path,int status=200,const httplib::Headers* authorization=nullptr){const auto result=client.Get(path,authorization?*authorization:bearer);if(!result||result->status!=status)throw std::runtime_error("Unexpected real context GET status method=GET path="+path+" expected="+std::to_string(status)+" actual="+(result?std::to_string(result->status):"no_response"));return Json::parse(result->body);}
    Json post(const std::string& path,const Json& body,int status=202,const httplib::Headers* authorization=nullptr){return raw(path,body.dump(),status,authorization);}
    Json raw(const std::string& path,const std::string& body,int status,const httplib::Headers* authorization=nullptr){const auto result=client.Post(path,authorization?*authorization:bearer,body,"application/json");require(result&&result->status==status,"Unexpected real context POST status");return Json::parse(result->body);}
    void submit(const std::string& id,const std::string& session,const std::string& prompt,const std::string& model=model_a){post("/v1/runs",{{"id",id},{"session_id",session},{"prompt",prompt},{"model_id",model}});}
    Json compact(const std::string& session,const std::string& id,const std::string& model=model_a,const httplib::Headers* authorization=nullptr){return post("/v1/sessions/"+session+"/context/compact",{{"id",id},{"model_id",model},{"expected_head_revision",0}},202,authorization);}
    Json request(const std::string& session,const std::string& id,const std::string& model=model_a){return get("/v1/sessions/"+session+"/context/requests/"+id+"?model_id="+model);}
    void cancel(const std::string& id){post("/v1/runs/"+id+"/cancel",Json::object());}
};
void seed(PersistenceService& store,AgentService& service,Http& http,const std::string& mode,const std::string& scope="owned"){
    store.create_session(scope,"Synthetic owned context-control conversation").get();
    for(int i=0;i<3;++i){const auto id=scope+"_seed_"+std::to_string(i);http.submit(id,scope,"fixture:"+mode+":"+scope+":seed:"+std::to_string(i));eventually([&]{return store.run(id).get().state==RunState::completed&&service.idle();},"Actual seed Agent did not retire");}
    require(store.history(scope).get().size()==6,"Three actual seed roots require six original messages");
}
void public_context(const Json& value,bool committed){
    const std::vector<std::string> keys={"session_id","model_id","enabled","automatic","head_revision","source_watermark","manual","checkpoint"};
    require(value.is_object()&&value.size()==keys.size(),"Public context observation must be a fixed projection");for(const auto& field:keys)require(value.contains(field),"Public context field missing");
    require(value["enabled"]==true&&value["automatic"]==false&&value["model_id"]==model_a,"Public policy must reflect actual native manual-only route");
    const auto raw=value.dump();for(const auto* private_name:{"encrypted_content","provider_items","actual_response_json","projection_json","authority_identity","provider_identity_json","source_binding","protected_user_binding","synthetic-private-opaque","credential_id", "provider-key"})require(raw.find(private_name)==std::string::npos,"Context DTO leaked private receipt/binding/credential material");
    if(committed){const auto& checkpoint=value.at("checkpoint");require(value["head_revision"]==1&&checkpoint.is_object()&&checkpoint.size()==4&&checkpoint.contains("id")&&checkpoint.contains("provider_elapsed_ms")&&checkpoint.contains("preparation_elapsed_ms")&&checkpoint.contains("usage"),"Committed context status requires actual fixed checkpoint metrics");require(checkpoint["usage"]==Json{{"input_tokens",31},{"output_tokens",7},{"total_tokens",38}},"Maintenance usage must be supplied actual response metrics, never inference/count aggregation");require(checkpoint["provider_elapsed_ms"].is_number_integer()&&checkpoint["provider_elapsed_ms"]>=0&&checkpoint["preparation_elapsed_ms"].is_number_integer()&&checkpoint["preparation_elapsed_ms"]>=checkpoint["provider_elapsed_ms"],"Maintenance clocks must record actual native timing");}
    else require(value["head_revision"]==0&&value["checkpoint"].is_null(),"Uncommitted control must not invent a checkpoint");
}
struct Owned {
    PersistenceService store;AgentService service;HttpServer server;int port;Serving serving;Http http;
    Owned(const std::filesystem::path& root,const std::vector<std::string>& imports,const std::string& base,const std::string& mode,bool credential=true)
        :store((root/"state.sqlite").string(),imports),service(store,prepare(store,base,mode,credential),1,8),server(store,token,&service),port(server.bind(0)),serving(server),http(port){}
    static AgentSettings prepare(PersistenceService& store,const std::string& base,const std::string& mode,bool credential){if(credential)put_key(store);return settings(base,mode);}
};
void idle_controller(const std::filesystem::path& root,const std::vector<std::string>& imports,const std::string& base){std::vector<Message> original;{
    Owned owner(root,imports,base,"idle");stage="idle_seed";seed(owner.store,owner.service,owner.http,"idle");original=owner.store.history("owned").get();auto& http=owner.http;
    public_context(http.get("/v1/sessions/owned/context?model_id="+model_a),false);
    httplib::Headers unauthenticated,wrong{{"Authorization","Bearer synthetic-wrong-token"}},origin=http.bearer;origin.emplace("Origin","http://127.0.0.1:42424");
    http.get("/v1/sessions/owned/context",401,&unauthenticated);
    http.post("/v1/sessions/owned/context/compact",{{"id","unauthorized"},{"expected_head_revision",0}},401,&wrong);
    http.get("/v1/sessions/owned/context",403,&origin);
    http.get("/v1/sessions/owned/context?extra=1",400);
    http.get("/v1/sessions/owned/context?model_id="+model_a+"&model_id="+model_a,400);
    http.get("/v1/sessions/owned/context?model_id=unconfigured",400);
    http.raw("/v1/sessions/owned/context/compact",R"({"id":"duplicate-field","expected_head_revision":0,"expected_head_revision":0})",400);
    http.post("/v1/sessions/owned/context/compact",{{"id","client-actor"},{"expected_head_revision",0},{"actor","model-supplied-actor"}},400);
    http.post("/v1/sessions/owned/context/compact",{{"id","missing-head"}},400);
    http.post("/v1/sessions/owned/context/compact",{{"id","wrong-head"},{"expected_head_revision",1}},409);
    require(count(root,"count")==0&&count(root,"compact")==0&&count(root,"inference")==3,"Rejected auth/schema/revision requests must not dispatch maintenance or inference");
    stage="idle_raw_query_validation";
    // The independent Node peer sends raw HTTP request targets, including '?'
    // and malformed escapes that the C++ client's encoder would normalize.
    {std::ofstream port(root/"raw-controller-port.txt",std::ios::binary);port<<owner.port;require(static_cast<bool>(port),"Owned raw-query controller port write failed");}
    flag(root,"raw-query-checks-ready");marker(root,"raw-query-checks-completed");
    {std::ifstream result(root/"raw-query-result.txt",std::ios::binary);std::string value;result>>value;require(value=="passed","Independent raw-target context query checks failed");}
    const auto binding=AgentRunner(owner.store,settings(base,"idle")).context_binding(model_a);
    require(!owner.store.context_current_manual_request({ContextScopeKind::session,"owned"},binding).get()&&owner.store.runs("owned").get().size()==3,"Malformed raw queries cannot create a manual request or Run");
    exact_history(original,owner.store.history("owned").get());public_context(http.get("/v1/sessions/owned/context"),false);
    require(count(root,"count")==0&&count(root,"compact")==0&&count(root,"inference")==3,"Malformed raw queries cannot dispatch provider maintenance or inference");
    stage="idle_auth_and_idempotency";
    // Native durable view credentials are the backend half of the browser's
    // HttpOnly-cookie adapter. No browser/cookie UI acceptance is claimed.
    const auto issued=http.post("/v1/view-sessions",{{"origin","http://127.0.0.1:42424"}},200);
    httplib::Headers view{{"Authorization","View "+issued.at("credential").get<std::string>()},{"X-XMind-View-Origin","http://127.0.0.1:42424"}};
    public_context(http.get("/v1/sessions/owned/context",200,&view),false);
    http.get("/v1/sessions/owned/context/compact",401,&view);
    http.post("/v1/sessions/owned/context/requests/never",Json::object(),401,&view);
    http.compact("owned","manual-idle",model_a,&view);marker(root,"owned-count-started");
    require(http.compact("owned","manual-idle")["state"]=="claimed"&&http.request("owned","manual-idle")["state"]=="claimed","Same authenticated request is once-bound during actual count dispatch");
    http.post("/v1/sessions/owned/context/compact",{{"id","manual-idle"},{"expected_head_revision",1},{"model_id",model_a}},409);
    owner.store.create_session("other","Synthetic isolation target").get();http.get("/v1/sessions/other/context/requests/manual-idle?model_id="+model_a,404);
    http.post("/v1/sessions/other/context/compact",{{"id","manual-idle"},{"expected_head_revision",0},{"model_id",model_a}},409);
    const auto original_runs=owner.store.runs("owned").get().size();
    http.post("/v1/runs",{{"id","excluded-root"},{"session_id","owned"},{"prompt","Synthetic rejected concurrent root"},{"model_id",model_a}},409);
    rejects<NotFound>([&]{owner.store.run("excluded-root").get();},"Idle owner exclusion cannot create a run");
    require(owner.store.runs("owned").get().size()==original_runs&&owner.store.history("owned").get().size()==original.size()&&count(root,"count")==1&&count(root,"compact")==0,"Lease/CAS/isolation rejects preserve exact history and a single actual count");
    flag(root,"release-owned-count");eventually([&]{return http.request("owned","manual-idle")["state"]=="completed"&&owner.service.idle();},"Actual idle controller did not publish its checkpoint");
    public_context(http.get("/v1/sessions/owned/context"),true);exact_history(original,owner.store.history("owned").get());
    require(owner.store.runs("owned").get().size()==3&&owner.store.operations("owned_seed_0").get().empty()&&count(root,"inference")==3&&count(root,"count")==2&&count(root,"compact")==1,"Idle maintenance cannot fabricate a Run, prompt, tool effect or inference");
    require(http.compact("owned","manual-idle")["state"]=="completed","Exact completed request replay returns its actual outcome despite later head revision");
    require(count(root,"count")==2&&count(root,"compact")==1,"Completed request replay cannot redispatch maintenance");
    owner.service.close();
} {
    stage="idle_reopen";Owned owner(root,imports,base,"idle",false);public_context(owner.http.get("/v1/sessions/owned/context"),true);
    require(owner.http.request("owned","manual-idle")["state"]=="completed"&&owner.http.compact("owned","manual-idle")["state"]=="completed"&&owner.service.idle(),"Reopen and exact replay inspect the old completed controller without dispatch");
    exact_history(original,owner.store.history("owned").get());require(count(root,"inference")==3&&count(root,"count")==2&&count(root,"compact")==1,"Reopen must not replay a count/compaction/inference");
}
}
void queued_cancel(const std::filesystem::path& root,const std::vector<std::string>& imports,const std::string& base){
    stage="queued_cancel";
    Owned owner(root,imports,base,"queued-cancel");seed(owner.store,owner.service,owner.http,"queued-cancel");owner.store.create_session("blocker","Synthetic real worker blocker").get();
    owner.http.submit("blocker-root","blocker","fixture:queued-cancel:blocker:hold");marker(root,"blocker-inference-started");
    owner.http.submit("queued-root","owned","fixture:queued-cancel:owned:queued");require(owner.store.run("queued-root").get().state==RunState::queued,"A queued cancellation must actually occupy the bounded native scheduler");
    owner.http.compact("owned","manual-queued");require(owner.http.request("owned","manual-queued")["state"]=="pending","Queued owner request is a real durable waiting ticket");
    owner.http.cancel("queued-root");eventually([&]{return owner.store.run("queued-root").get().state==RunState::cancelled;},"Actual queued root was not cancelled");
    const auto original=owner.store.history("owned").get();eventually([&]{return owner.http.request("owned","manual-queued")["state"]=="completed";},"Queued cancellation lost its waiting manual ticket");
    exact_history(original,owner.store.history("owned").get());require(original.size()==7&&count(root,"inference")==3&&count(root,"count")==2&&count(root,"compact")==1,"Cancelled queued root retains its original user and no fabricated response/provider dispatch");
    public_context(owner.http.get("/v1/sessions/owned/context"),true);owner.http.cancel("blocker-root");eventually([&]{return owner.store.run("blocker-root").get().state==RunState::cancelled&&owner.service.idle();},"Actual blocker transport did not drain");
}
void active_finish(const std::filesystem::path& root,const std::vector<std::string>& imports,const std::string& base){
    stage="active_finish";
    Owned owner(root,imports,base,"active-finish");seed(owner.store,owner.service,owner.http,"active-finish");
    owner.http.submit("active-root","owned","fixture:active-finish:owned:hold");marker(root,"owned-inference-started");
    owner.http.compact("owned","manual-active");require(owner.http.request("owned","manual-active")["state"]=="pending"&&count(root,"count")==0,"Active request waits for the real next safe owner boundary");
    flag(root,"release-owned-inference");eventually([&]{return owner.store.run("active-root").get().state==RunState::completed;},"Actual active root did not finish");
    const auto original=owner.store.history("owned").get();eventually([&]{return owner.http.request("owned","manual-active")["state"]=="completed"&&owner.service.idle();},"Final response without a next model turn lost its waiting manual ticket");
    exact_history(original,owner.store.history("owned").get());require(original.size()==8&&count(root,"inference")==4&&count(root,"count")==2&&count(root,"compact")==1,"Post-final safe idle wake cannot invent another inference/assistant");public_context(owner.http.get("/v1/sessions/owned/context"),true);
}
void model_race(const std::filesystem::path& root,const std::vector<std::string>& imports,const std::string& base){
    stage="model_race";
    Owned owner(root,imports,base,"model-race");owner.store.create_session("owned","Synthetic fresh foreign-model race target").get();seed(owner.store,owner.service,owner.http,"model-race","other");
    owner.http.compact("other","blocking-maintenance");marker(root,"other-count-started");
    owner.http.compact("owned","bound-model-a",model_a);
    owner.http.submit("model-b-root","owned","fixture:model-race:owned:model-b-hold",model_b);marker(root,"owned-inference-started");
    flag(root,"release-other-count");eventually([&]{return owner.http.request("other","blocking-maintenance")["state"]=="completed";},"Other actual maintenance owner did not release its worker");
    require(owner.http.request("owned","bound-model-a")["state"]=="pending"&&count(root,"count")==0&&count(root,"compact")==0&&count(root,"count","owned",model_b)==0,"Model-A ticket cannot be erased or transferred to the model-B root");
    owner.http.cancel("model-b-root");eventually([&]{return owner.store.run("model-b-root").get().state==RunState::cancelled;},"Racing model-B physical transport did not retire");
    const auto original=owner.store.history("owned").get();eventually([&]{return owner.http.request("owned","bound-model-a")["state"]=="failed"&&owner.service.idle();},"Exact model-A ticket was lost instead of retiring its actual irreducible source after model-B cancellation");
    exact_history(original,owner.store.history("owned").get());require(original.size()==1&&count(root,"count")==0&&count(root,"compact")==0&&count(root,"inference")==0&&count(root,"inference","owned",model_b)==1&&count(root,"count","owned",model_b)==0&&count(root,"compact","owned",model_b)==0,"Bound ticket preserves the original cancelled user and cannot dispatch paid maintenance to another model or compact a nonexistent prefix");
    XlangSqlite sql((root/"state.sqlite").string(),imports);const auto refusal=sql.execute("SELECT failure_code FROM context_manual_requests WHERE id='bound-model-a'").rows;require(refusal.size()==1&&std::get<std::string>(refusal[0][0])=="capacity_exceeded","Irreducible bound ticket must retain its truthful typed capacity failure");
    public_context(owner.http.get("/v1/sessions/owned/context?model_id="+model_a),false);
}
std::int64_t scalar(XlangSqlite& sql,const std::string& query){const auto rows=sql.execute(query).rows;require(rows.size()==1&&rows[0].size()==1&&std::holds_alternative<std::int64_t>(rows[0][0]),"Actual native scalar query failed");return std::get<std::int64_t>(rows[0][0]);}
void shutdown(const std::filesystem::path& root,const std::vector<std::string>& imports,const std::string& base){std::vector<Message> owned_original,other_original;{
    stage="shutdown";
    Owned owner(root,imports,base,"shutdown");seed(owner.store,owner.service,owner.http,"shutdown");seed(owner.store,owner.service,owner.http,"shutdown","other");owned_original=owner.store.history("owned").get();other_original=owner.store.history("other").get();
    owner.http.compact("other","shutdown-active");marker(root,"other-count-started");owner.http.compact("owned","shutdown-queued");
    require(owner.http.request("owned","shutdown-queued")["state"]=="pending","Shutdown must include one real undispatched context ticket");owner.service.close();
    const auto binding=AgentRunner(owner.store,settings(base,"shutdown")).context_binding(model_a);
    require(owner.store.context_manual_request("shutdown-active",binding).get().state=="failed"&&owner.store.context_manual_request("shutdown-queued",binding).get().state=="failed","Shutdown drains actual count/compaction ownership and cancels unowned queued tickets");
    exact_history(owned_original,owner.store.history("owned").get());exact_history(other_original,owner.store.history("other").get());
} {
    Owned owner(root,imports,base,"shutdown",false);const auto binding=AgentRunner(owner.store,settings(base,"shutdown")).context_binding(model_a);
    require(owner.store.context_manual_request("shutdown-active",binding).get().state=="failed"&&owner.store.context_manual_request("shutdown-queued",binding).get().state=="failed"&&owner.service.idle(),"Certain shutdown outcomes remain retired on restart");
    public_context(owner.http.get("/v1/sessions/owned/context"),false);exact_history(owned_original,owner.store.history("owned").get());exact_history(other_original,owner.store.history("other").get());
    require(count(root,"count")==0&&count(root,"compact")==0&&count(root,"count","other")==1&&count(root,"compact","other")==0,"Restart cannot replay cancelled count or undispatched maintenance");
}
}
void uncertain(const std::filesystem::path& root,const std::vector<std::string>& imports,const std::string& base){std::vector<Message> original;{
    stage="uncertain";
    Owned owner(root,imports,base,"uncertain");seed(owner.store,owner.service,owner.http,"uncertain");original=owner.store.history("owned").get();
    {XlangSqlite fault((root/"state.sqlite").string(),imports);fault.execute("CREATE TRIGGER synthetic_context_receipt_fault BEFORE INSERT ON context_checkpoints BEGIN SELECT RAISE(ABORT,'synthetic actual acknowledgement journal fault'); END");}
    owner.http.compact("owned","uncertain-receipt");marker(root,"owned-compact-acknowledged");eventually([&]{return !owner.service.healthy();},"Acknowledgement journal failure did not fail-stop the actual controller");
    require(owner.http.get("/v1/health")["status"]=="degraded","Unrecorded maintenance cannot advertise a healthy executor");owner.http.get("/v1/sessions/owned/context",503);owner.service.close();
    exact_history(original,owner.store.history("owned").get());
    {XlangSqlite sql((root/"state.sqlite").string(),imports);require(scalar(sql,"SELECT count(*) FROM context_compactions WHERE state='started'")==1&&scalar(sql,"SELECT count(*) FROM context_checkpoints")==0&&scalar(sql,"SELECT sum(revision) FROM context_heads")==0,"Lost durable acknowledgement cannot be fabricated as failed/succeeded/head-installed");sql.execute("DROP TRIGGER synthetic_context_receipt_fault");}
} {
    Owned owner(root,imports,base,"uncertain",false);const auto binding=AgentRunner(owner.store,settings(base,"uncertain")).context_binding(model_a);
    require(owner.store.context_manual_request("uncertain-receipt",binding).get().state=="interrupted"&&owner.service.idle(),"Recovery quarantines unfinished actual maintenance without automatic replay");public_context(owner.http.get("/v1/sessions/owned/context"),false);exact_history(original,owner.store.history("owned").get());
    {XlangSqlite sql((root/"state.sqlite").string(),imports);require(scalar(sql,"SELECT count(*) FROM context_compactions WHERE state='interrupted'")==1&&scalar(sql,"SELECT count(*) FROM context_idle_owners WHERE state='interrupted'")==1&&scalar(sql,"SELECT count(*) FROM runs")==3,"Actual interrupted owner is independent of normal Runs and does not acquire a replay allowance");}
    require(count(root,"inference")==3&&count(root,"count")==1&&count(root,"compact")==1,"Acknowledged maintenance fault recovery cannot send a prospective count or provider replay");
}
}
}
int main(int argc,char** argv)try{
    if(argc!=6)throw std::invalid_argument("Expected owned root, modules, stdlib, loopback provider and mode");
    const std::filesystem::path root=std::filesystem::u8path(argv[1]);const std::vector<std::string> imports{argv[2],argv[3]};const std::string mode=argv[5];
    if(mode=="idle")idle_controller(root,imports,argv[4]);else if(mode=="queued-cancel")queued_cancel(root,imports,argv[4]);else if(mode=="active-finish")active_finish(root,imports,argv[4]);else if(mode=="model-race")model_race(root,imports,argv[4]);else if(mode=="shutdown")shutdown(root,imports,argv[4]);else if(mode=="uncertain")uncertain(root,imports,argv[4]);else throw std::invalid_argument("Unknown controller fixture mode");
    std::cout<<"Native context-control boundary passed real authenticated AgentService/HTTP/maintenance/SQLite ownership; provider messages/counts/canonical replies and credentials were synthetic.\n";return 0;
}catch(const std::exception& error){if(argc>1)try{std::ofstream private_log(std::filesystem::u8path(argv[1])/"private-native-contract-error.log",std::ios::binary);private_log<<error.what();}catch(...){}std::cerr<<"context_control_contract_failed stage="<<stage<<"\n";return 1;}
