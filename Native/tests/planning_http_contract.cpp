// Synthetic signed assistant DTOs and a synthetic controller exercise only the
// real native HTTP / typed persistence boundary. SQLite, migrations, ownership,
// CAS, pause and answer transactions are actual xlang3 native operations in an
// owned disposable directory. No AgentRunner/provider/file-effect claim.
#include "agentflow/http_server.hpp"
#include "agentflow/dynamic_plan.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include <chrono>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <random>
#include <thread>

using namespace agentflow;
using Json=nlohmann::json;
namespace {
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
struct Directory {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("xmind-planning-http-"+std::to_string(std::random_device{}()));
    Directory(){if(!std::filesystem::create_directory(path))throw std::runtime_error("Fixture directory already exists");}
    ~Directory(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
struct Serving {
    HttpServer& server;std::thread worker;
    explicit Serving(HttpServer& value):server(value),worker([&value]{value.listen();}){}
    ~Serving(){server.stop();worker.join();}
};
const std::string authority(64,'a');
std::int64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
struct Controller final:RunExecutor {
    PersistenceService& store;std::atomic<int> input_calls=0,resume_calls=0,continuation_calls=0;
    explicit Controller(PersistenceService& value):store(value){}
    Run submit(std::string,std::string,std::string)override{throw RunUnavailable("Synthetic controller does not execute prompts");}
    void cancel(const std::string&)override{throw RunUnavailable("Synthetic controller does not execute prompts");}
    bool healthy()const override{return true;}
    bool available()const override{return false;}
    bool supports_dynamic_planning()const override{return true;}
    Run plan_input(const std::string& root,const std::string& request,std::string input,const std::string& principal,std::int64_t revision,std::int64_t sequence)override{
        ++input_calls;require(principal=="local-owner","HTTP must derive the local owner actor");
        const auto p=store.dynamic_plan_for_root(root).get();require(p.has_value(),"Controller requires an actual plan");
        store.input_dynamic_human({p->id,request,std::move(input),principal,authority,revision,sequence}).get();return store.run(root).get();
    }
    Run resume_plan(const std::string& root,const std::string& principal,std::int64_t revision,std::int64_t sequence)override{
        ++resume_calls;require(principal=="local-owner","Resume must derive the local owner actor");
        const auto p=store.dynamic_plan_for_root(root).get();require(p.has_value(),"Controller requires an actual plan");
        // Explicit synthetic physical-owner action: opens the typed segment but
        // does not claim a model continuation or execute another child.
        return store.resume_dynamic_owner({p->id,"resumed_"+root,authority,revision,sequence,store.root_budget(root).get().revision}).get().run;
    }
    Run continue_a2a_task(std::string task,std::string message,std::string context,std::string identity,std::string content,std::string payload,std::string input)override{
        ++continuation_calls;if(const auto replay=store.a2a_continuation(message,task,context,identity,content,payload).get())return *replay;const auto p=store.dynamic_plan_for_root(task).get();require(p.has_value(),"A2A continuation requires a real dynamic plan");std::vector<DynamicHumanRequest> waiting;for(const auto& request:store.dynamic_human_requests(p->id).get())if(request.state=="waiting")waiting.push_back(request);require(waiting.size()==1,"A2A continuation requires one actual pending human question");DynamicHumanInputSpec spec{p->id,waiting.front().id,std::move(input),"a2a-owner",authority,p->revision,p->state_sequence};return store.continue_a2a_message(spec,task,message,context,identity,content,payload).get();
    }
};
void seed(PersistenceService& store,const std::string& root,bool coding,bool expose_nested=false){
    const std::string coding_child=root=="nested_http"?"nested_http_coding_child":"coding_child";
    const std::string coding_claim=root=="nested_http"?"nested_http_claim_coding":"claim_coding";
    const auto provider=Json{{"model_id","synthetic-http-model"},{"wire","responses"}}.dump();
    const RootBudgetSpec budget{"native.dynamic-plan",1,"synthetic-http-workspace",provider,8,2,32,600000};
    DynamicPlanCapabilities caps;caps.backend_identity=authority;caps.workspace_identity=budget.workspace_identity;caps.provider_identity_json=budget.provider_identity_json;caps.tool_catalog_json="{}";caps.max_nodes=17;caps.max_revisions=5;caps.max_humans=3;caps.max_parent_turns=7;caps.presets={{"workspace.inspect",1,true,{},4,{}},{"workspace.coding",1,false,{},9,{}}};
    store.create_session("session_"+root,"Synthetic planning HTTP boundary").get();store.start_prompt_run(root,"session_"+root,R"({"content":"Synthetic planning HTTP boundary"})",budget,caps).get();store.transition(root,RunState::queued,RunState::running).get();
    require(store.dynamic_capabilities(root).get().provider_identity_json==provider&&store.root_budget(root).get().spec.provider_identity_json==provider,"Captured capability and budget must bind identical canonical provider identity bytes");
    store.open_dynamic_budget_segment({root,"segment_"+root,authority,store.root_budget(root).get().revision}).get();
    const std::vector<DynamicPresetCapability> presets{{"workspace.inspect",1,true,{"read_file"},4,std::string(64,'b')},{"workspace.coding",1,false,{"read_file","edit_file"},9,std::string(64,'c')}};
    store.finalize_dynamic_capabilities(root,authority,R"({"tools":[{"name":"read_file"},{"name":"edit_file"}],"private_marker":"synthetic-private-catalog"})",presets).get();
    auto nodes=Json::array();if(coding)nodes.push_back({{"id","coding"},{"type","agent"},{"objective","Synthetic coding DTO only"},{"preset","workspace.coding"},{"depends_on",Json::array()}});
    nodes.push_back({{"id","answer"},{"type","human"},{"question","Synthetic public question"},{"depends_on",coding?Json::array({{{"task","coding"},{"require","success"}}}):Json::array()}});
    const auto raw=Json{{"expected_revision",0},{"expected_state_sequence",0},{"add",nodes}}.dump();const auto call="provider_"+root,attempt="attempt_"+root;
    const auto actual=Json{{"content",""},{"model","synthetic-http-model"},{"tool_calls",Json::array({{{"id",call},{"name","plan_tasks"},{"arguments",raw}}})},{"usage",{{"input_tokens",13},{"output_tokens",3},{"output_tokens_details",{{"reasoning_tokens",2},{"private_numeric",91}}},{"private_numeric",99}}},{"elapsed_ms",31},{"first_token_ms",12},{"provider_items_json",R"([{"type":"reasoning","encrypted_content":"synthetic-private-opaque"}])"},{"provider_private_context","synthetic-private-context"}}.dump();
    store.reserve_model_call(root,root,attempt,ModelCallRole::parent).get();store.start_model_call(root,root,attempt).get();store.finish_model_call(root,root,attempt,actual).get();store.append_event(root,"tool.started",Json{{"activity_id",root+":plan:"+call},{"call_id",call},{"name","plan_tasks"},{"arguments",Json::parse(raw)}}.dump()).get();
    DynamicPlanChangeSpec change;change.id="call_"+root;change.plan_id="plan_"+root;change.root_run_id=root;change.provider_tool_call_id=call;change.origin_attempt_id=attempt;change.arguments_json=raw;change.parent_assistant_json=actual;change.backend_identity=authority;change.expected_budget_revision=store.root_budget(root).get().revision;
    const auto accepted=store.accept_dynamic_plan_change(change).get();
    if(coding){
        const auto p=store.dynamic_plan(accepted.plan_id).get();const auto ready=prepare_dynamic_node(p,"coding");DynamicFrontierSpec frontier;frontier.plan_id=p.id;frontier.plan_call_id=accepted.id;frontier.backend_identity=authority;frontier.expected_revision=p.revision;frontier.expected_state_sequence=p.state_sequence;frontier.expected_budget_revision=store.root_budget(root).get().revision;frontier.tasks.push_back({"coding",coding_claim,coding_child,{},ready.dependency_outputs_json,ready.node.definition_revision});store.admit_dynamic_frontier(frontier).get();
        store.transition(coding_child,RunState::queued,RunState::running).get();
        if(expose_nested){
            const auto nested_args=Json{{"expected_revision",0},{"expected_state_sequence",0},{"add",Json::array({{{"id","inner"},{"type","agent"},{"objective","Synthetic nested owner objective"},{"preset","workspace.inspect"},{"depends_on",Json::array()}}})}}.dump();const std::string nested_tool="provider_nested",nested_attempt="attempt_nested";
            const auto nested_actual=Json{{"content",""},{"model","synthetic-http-model"},{"tool_calls",Json::array({{{"id",nested_tool},{"name","plan_tasks"},{"arguments",nested_args}}})},{"usage",{{"input_tokens",8},{"output_tokens",2}}},{"elapsed_ms",17},{"first_token_ms",6}}.dump();
            store.reserve_model_call(root,coding_child,nested_attempt,ModelCallRole::leaf).get();store.start_model_call(root,coding_child,nested_attempt).get();store.finish_model_call(root,coding_child,nested_attempt,nested_actual).get();store.append_event(coding_child,"tool.started",Json{{"activity_id",coding_child+":plan:"+nested_tool},{"call_id",nested_tool},{"name","plan_tasks"},{"arguments",Json::parse(nested_args)},{"arguments_json",nested_args}}.dump()).get();
            DynamicPlanChangeSpec nested;nested.id="nested_call_"+root;nested.plan_id="nested_plan_"+root;nested.root_run_id=root;nested.owner_run_id=coding_child;nested.provider_tool_call_id=nested_tool;nested.origin_attempt_id=nested_attempt;nested.arguments_json=nested_args;nested.parent_assistant_json=nested_actual;nested.backend_identity=authority;nested.expected_budget_revision=store.root_budget(root).get().revision;store.accept_dynamic_plan_change(nested).get();return;
        }
        store.complete_run(coding_child,R"({"content":"Synthetic child completion, no actual edit","model":"synthetic-http-model","usage":{"input_tokens":4,"output_tokens":2},"elapsed_ms":19,"provider_items_json":"synthetic-private-child-opaque"})").get();store.settle_dynamic_child(coding_child).get();
    }
    auto p=store.dynamic_plan(accepted.plan_id).get();store.publish_dynamic_human({"question_"+root,p.id,"answer",authority,accepted.id,p.revision,p.state_sequence,now()+600000}).get();p=store.dynamic_plan(p.id).get();store.suspend_dynamic_owner({p.id,accepted.id,"segment_"+root,authority,p.revision,p.state_sequence,store.root_budget(root).get().revision,17}).get();
}
void contract(PersistenceService& store){
    seed(store,"owner",true);seed(store,"other",false);seed(store,"nested_http",true,true);store.create_session("historical_session","Historical ordinary fixture").get();store.start_prompt_run("historical","historical_session",R"({"content":"Synthetic old ordinary root"})").get();
    const std::string token="synthetic-planning-http-access-token-32-bytes";Controller controller(store);HttpServer server(store,token,&controller);const auto port=server.bind(0);Serving serving(server);
    httplib::Client client("127.0.0.1",port);client.set_connection_timeout(2,0);client.set_read_timeout(5,0);client.set_follow_location(false);const httplib::Headers headers{{"Authorization","Bearer "+token}};
    for(int attempt=0;attempt<20;++attempt){if(client.Get("/v1/health",headers))break;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    auto get=[&](const std::string& path,int status=200){const auto response=client.Get(path,headers);require(response&&response->status==status,"Unexpected actual HTTP GET status");return Json::parse(response->body);};
    auto post=[&](const std::string& path,const std::string& raw,int status){const auto response=client.Post(path,headers,raw,"application/json");require(response&&response->status==status,"Unexpected actual HTTP POST status");return Json::parse(response->body);};
    auto rpc=[&](const std::string& method,const Json& params,const std::string& version="1.0"){const httplib::Headers a2a_headers{{"Authorization","Bearer "+token},{"A2A-Version",version}};const auto response=client.Post("/a2a",a2a_headers,Json{{"jsonrpc","2.0"},{"id","http-"+method},{"method",method},{"params",params}}.dump(),"application/json");require(response&&response->status==200,"Unexpected A2A HTTP response");return Json::parse(response->body);};
    require(get("/v1/health")["agent_planning"]==true&&get("/v1/health")["agent_execution"]==false,"Planning must reflect the executor capability rather than invented execution availability");
    require(get("/v1/agent/planning")==Json{{"enabled",true},{"tools",Json::array({"inspect_plan","plan_tasks","revise_plan"})}},"Planning metadata must not invent captured presets/limits");
    auto snapshot=get("/v1/runs/owner/plan");const auto encoded=snapshot.dump();
    const auto nested_snapshot=get("/v1/runs/nested_http/plan");require(nested_snapshot["plan"]["id"]=="plan_nested_http"&&nested_snapshot["calls"].size()==1&&nested_snapshot["nested_plans"].size()==1&&nested_snapshot["nested_plans"][0]["owner_run_id"]=="nested_http_coding_child"&&nested_snapshot["nested_plans"][0]["parent_plan_id"]=="plan_nested_http"&&nested_snapshot["nested_plans"][0]["parent_node_label"]=="coding"&&nested_snapshot["nested_plans"][0]["depth"]==1&&nested_snapshot["nested_plans"][0]["run"]["state"]=="running"&&nested_snapshot["nested_plans"][0]["plan"]["nodes"][0]["id"]=="inner"&&nested_snapshot["nested_plans"][0]["calls"].size()==1&&nested_snapshot["nested_plans"][0]["calls"][0]["response"]["usage"]["input_tokens"]==8,"Root planning observation must expose actual child-owned plan topology, call metrics and exact parent binding");
    for(const auto* private_text:{"synthetic-private-opaque","synthetic-private-context","synthetic-private-catalog","synthetic-private-child-opaque","backend_identity","provider_identity_json","tool_catalog_json","parent_assistant_json","provider_items_json","private_numeric"})require(encoded.find(private_text)==std::string::npos,"Public planning projection leaked private material");
    require(snapshot["policy"]["limits"]["max_nodes"]==17&&snapshot["policy"]["limits"]["max_parent_turns"]==7&&snapshot["policy"]["presets"][1]["readonly"]==false&&snapshot["policy"]["presets"][1]["turn_limit"]==9,"GET must use actual captured registered policy");
    require(snapshot["calls"].size()==1&&snapshot["calls"][0]["pending"]==true&&snapshot["calls"][0]["response"]["usage"]==Json{{"input_tokens",13},{"output_tokens",3},{"output_tokens_details",{{"reasoning_tokens",2}}}}&&snapshot["calls"][0]["response"]["elapsed_ms"]==31&&snapshot["calls"][0]["response"]["first_token_ms"]==12,"Pending metrics must come from the actual saved synthetic DTO without aggregation");
    require(snapshot["revisions"].size()==1&&snapshot["revisions"][0]["call_id"]==snapshot["calls"][0]["id"]&&snapshot["revisions"][0]["accepted_event_seq"]==snapshot["calls"][0]["accepted_event_seq"]&&snapshot["revisions"][0]["spec"]["nodes"].size()==2,"Accepted revision history must correlate actual call/event/topology");
    const auto children=get("/v1/runs/owner/children");require(children.size()==1&&children[0]["kind"]=="dynamic_agent"&&children[0]["preset_id"]=="workspace.coding"&&children[0]["plan_id"]=="plan_owner"&&children[0]["node_label"]=="coding"&&children[0]["claim_id"]=="claim_coding"&&children[0]["definition_revision"]==children[0]["claim_revision"]&&!children[0].contains("readonly")&&!children[0].contains("backend_identity"),"Owned coding child DTO must retain actual claims without labelling it read only");
    get("/v1/runs/coding_child/plan",404);get("/v1/runs/missing/plan",404);get("/v1/runs/owner/plan?after=0",400);get("/v1/agent/planning?after=0",400);get("/v1/runs/owner/plan/resume",404);
    require(get("/v1/runs/historical/plan")["plan"].is_null()&&get("/v1/runs/historical/plan")["policy"].is_null(),"Historical ordinary roots must remain inspectable");
    auto absent=client.Get("/v1/runs/owner/plan");require(absent&&absent->status==401,"Plan inspection must retain native authentication");post("/v1/runs/owner/plan","{}",404);post("/v1/agent/planning","{}",404);
    const auto revision=snapshot["plan"]["revision"],sequence=snapshot["plan"]["state_sequence"];const std::string answer=" {\"answer\":18446744073709551617,\"decimal\":1.00000000000000000001} ";
    auto input=Json{{"input_json",answer},{"expected_revision",revision},{"expected_state_sequence",sequence}};
    const auto events=store.events("owner").get().size();
    for(const auto& changed:std::vector<Json>{Json{{"input",{{"answer",1}}},{"expected_revision",revision},{"expected_state_sequence",sequence}},Json{{"input_json",answer},{"actor","other"},{"expected_revision",revision},{"expected_state_sequence",sequence}},Json{{"input_json",answer},{"expected_revision",0},{"expected_state_sequence",sequence}},Json{{"input_json",answer},{"expected_revision",revision},{"expected_state_sequence",1.5}},Json{{"input_json",answer},{"expected_revision",revision}},Json{{"input_json","[]"},{"expected_revision",revision},{"expected_state_sequence",sequence}},Json{{"input_json","{\"a\":1,\"\\u0061\":2}"},{"expected_revision",revision},{"expected_state_sequence",sequence}}})post("/v1/runs/owner/plan/human/question_owner",changed.dump(),400);
    post("/v1/runs/owner/plan/human/question_owner",R"({"input_json":"{}","input_json":"{}","expected_revision":1,"expected_state_sequence":1})",400);
    post("/v1/runs/owner/plan/human/question_other",input.dump(),404);post("/v1/runs/coding_child/plan/human/question_owner",input.dump(),404);post("/v1/runs/owner/plan/human/question_owner?after=0",input.dump(),400);
    require(controller.input_calls==0&&store.events("owner").get().size()==events&&store.dynamic_human_request("plan_owner","question_owner").get().state=="waiting","Malformed/foreign inputs must never dispatch or mutate the owner");
    const auto pending=rpc("GetTask",{{"id","other"}});require(pending["result"]["status"]["message"]["parts"][0]["text"]=="Synthetic public question","A2A input-required task must expose the exact durable human question");
    const Json continuation={{"messageId","a2a-answer-other"},{"role","ROLE_USER"},{"taskId","other"},{"contextId","session_other"},{"parts",Json::array({{{"text","Answer through A2A"}}})}};const auto continued_params=Json{{"message",continuation},{"configuration",{{"returnImmediately",true},{"historyLength",8}}}};const auto continued=rpc("SendMessage",continued_params);require(continued["result"]["task"]["id"]=="other"&&continued["result"]["task"]["status"]["state"]=="TASK_STATE_INPUT_REQUIRED","A2A continuation must address and retain its same paused task until the owner resumes");const auto continued_history=continued["result"]["task"]["history"];const auto preserved=std::find_if(continued_history.begin(),continued_history.end(),[](const Json& item){return item.value("messageId",std::string{})=="a2a-answer-other";});require(preserved!=continued_history.end()&&(*preserved)["parts"]==continuation["parts"]&&(*preserved)["contextId"]=="session_other","Task history must preserve the exact admitted continuation message");const auto human_answer=store.dynamic_human_request("plan_other","question_other").get();require(human_answer.state=="answered"&&human_answer.actor=="a2a-owner"&&Json::parse(human_answer.input_json)==Json{{"answer","Answer through A2A"}},"A2A continuation must atomically answer its actual human node under the derived protocol actor");const auto after_answer=store.events("other").get().size();const auto replay=rpc("SendMessage",continued_params);require(replay["result"]["task"]["id"]=="other"&&store.events("other").get().size()==after_answer&&controller.continuation_calls==2,"Retrying an A2A continuation must return its same task without answering or auditing twice");auto changed=continued_params;changed["message"]["parts"][0]["text"]="changed answer";require(rpc("SendMessage",changed)["error"]["code"]==-32004&&store.events("other").get().size()==after_answer,"Changed continuation content must conflict without a second mutation");bool cross_kind_id_rejected=false;try{store.incoming_message("a2a-answer-other","session_other",continuation.dump(),"Answer through A2A").get();}catch(const Conflict&){cross_kind_id_rejected=true;}require(cross_kind_id_rejected,"An A2A message ID cannot be reused between initial admission and task continuation");
    auto stale=input;stale["expected_state_sequence"]=1;post("/v1/runs/owner/plan/human/question_owner",stale.dump(),409);require(store.events("owner").get().size()==events,"Stale native CAS must leave question/audit unchanged");
    post("/v1/runs/owner/plan/resume",Json{{"expected_revision",revision},{"expected_state_sequence",sequence},{"state","running"}}.dump(),400);post("/v1/runs/owner/plan/resume",Json{{"expected_revision",revision},{"expected_state_sequence",sequence}}.dump(),409);
    const auto answered=post("/v1/runs/owner/plan/human/question_owner",input.dump(),200);require(answered["id"]=="owner"&&answered["state"]=="paused","HTTP answer must return the actual same paused owner");
    require(store.dynamic_human_request("plan_owner","question_owner").get().input_json==answer&&store.dynamic_human_request("plan_owner","question_owner").get().actor=="local-owner","Native input must preserve raw numeric tokens and derive the actor");
    snapshot=get("/v1/runs/owner/plan");require(snapshot["questions"][0]["state"]=="answered"&&snapshot["questions"][0]["input_json"]==answer&&!snapshot["questions"][0].contains("actor")&&snapshot["plan"]["report_ready"]==true,"Zero-child final human answer must be inspectable and report ready");
    const auto resume=Json{{"expected_revision",snapshot["plan"]["revision"]},{"expected_state_sequence",snapshot["plan"]["state_sequence"]}};const auto resumed=post("/v1/runs/owner/plan/resume",resume.dump(),202);require(resumed["id"]=="owner"&&resumed["state"]=="running"&&store.dynamic_budget_segment("owner").get().ordinal==2,"Explicit synthetic owner resume must open one actual segment on the same root");post("/v1/runs/owner/plan/resume",resume.dump(),409);
    // Actual typed report/conversation commit remains separate from HTTP. The
    // next inspection must omit original pending-response metrics after commit.
    require(store.settle_dynamic_plan_step("call_owner").get().ready,"Actual final-human report should settle");store.commit_dynamic_tool_turn("owner","call_owner").get();snapshot=get("/v1/runs/owner/plan");require(snapshot["calls"][0]["pending"]==false&&!snapshot["calls"][0].contains("response"),"A committed original response must not be presented as pending");
    HttpServer disabled(store,token);const auto disabled_port=disabled.bind(0);Serving disabled_serving(disabled);httplib::Client disabled_client("127.0.0.1",disabled_port);const auto metadata=disabled_client.Get("/v1/agent/planning",headers);require(metadata&&metadata->status==200&&Json::parse(metadata->body)==Json{{"enabled",false},{"tools",Json::array()}},"Disabled executor must not advertise planning tools");
}
}
int main(int argc,char** argv){if(argc!=3)return 2;try{Directory directory;PersistenceService store((directory.path/"state.sqlite").string(),{argv[1],argv[2]});contract(store);std::cout<<"Actual native planning HTTP and xlang3 SQLite ownership/projection/CAS passed; synthetic DTO/controller only, no provider or agent execution\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
