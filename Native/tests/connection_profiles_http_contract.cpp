#include "connection_run_recording_fixture.hpp"
#include "agentflow/http_server.hpp"
#include "agentflow/connection_binding.hpp"
#include "agentflow/graph.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <random>
#include <thread>

using namespace agentflow;
using Json=nlohmann::json;

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct TemporaryDatabase {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("xmind-connections-http-"+std::to_string(std::random_device{}())+".sqlite");
    ~TemporaryDatabase(){for(const auto* suffix:{"","-wal","-shm"}){auto file=path;file+=suffix;std::error_code ignored;std::filesystem::remove(file,ignored);}}
};
// Recording execution peer: tests transport/native option dispatch only. No model
// inference or real process effects are claimed by this fixture.
using test::RecordingGraphExecutor;
struct Access {
    HttpServer server;
    int port;
    std::thread worker;
    Access(PersistenceService& store,const std::string& token,RunExecutor* executor=nullptr)
        :server(store,token,executor,nullptr,{},{},{},nullptr,dynamic_cast<GraphExecution*>(executor)),port(server.bind(0)),worker([this]{server.listen();}){
        require(port>0,"Connection profile HTTP server must bind");
    }
    ~Access(){server.stop();if(worker.joinable())worker.join();}
};
Json read_catalog(httplib::Client& client,const httplib::Headers& auth){
    const auto response=client.Get("/v1/connections",auth);
    require(response&&response->status==200,"Authenticated clients must read the Local profile catalog");
    return Json::parse(response->body);
}
}

int main(int argc,char** argv){
    if(argc!=3)return 2;
    try{
        TemporaryDatabase database;
        const std::string token="native-connection-profile-http-token-32-bytes";
        const httplib::Headers auth{{"Authorization","Bearer "+token}};
        Json saved;
        {
            PersistenceService store(database.path.string(),{argv[1],argv[2]});
            {
            RecordingGraphExecutor execution(store);Access access(store,token,&execution);
            httplib::Client client("127.0.0.1",access.port);
            client.set_connection_timeout(2,0);client.set_read_timeout(3,0);
            const auto unauthorized=client.Get("/v1/connections");
            require(unauthorized&&unauthorized->status==401,"Connection catalog must require local backend authentication");

            auto catalog=read_catalog(client,auth);
            require(catalog.at("revision")==1&&catalog.at("active_id")=="local"&&catalog.at("profiles").size()==1,
                "First authenticated read must initialize one active Local profile");
            const auto input=Json{{"expected_revision",1},{"profiles",Json::array({
                Json{{"id","local"},{"name","Local"},{"kind","local"},{"origin",""}},
                Json{{"id","company"},{"name","CantorAI Nexus"},{"kind","nexus"},{"origin","https://nexus.example.com/xmind"}}
            })}};
            const auto update=client.Post("/v1/connections",auth,input.dump(),"application/json");
            require(update&&update->status==200,"Authenticated client must be able to save a validated connection catalog");
            catalog=Json::parse(update->body);
            require(catalog.at("revision")==2&&catalog.at("active_id")=="local"&&catalog.at("profiles").size()==2,
                "Adding a Nexus endpoint must preserve the active Local connection");
            const auto& nexus=catalog.at("profiles")[1];
            require(nexus.at("status")=="not_connected"&&nexus.at("command_routing")==false&&nexus.at("authenticated")==false,
                "Saved Nexus metadata must not claim authentication or remote execution");
            require(update->body.find("access_token")==std::string::npos,"Connection catalog must not expose credential fields");

            const auto secret=Json{{"expected_revision",2},{"profiles",Json::array({
                Json{{"id","local"},{"name","Local"},{"kind","local"}},
                Json{{"id","company"},{"name","CantorAI Nexus"},{"kind","nexus"},{"origin","https://nexus.example.com/xmind"},{"access_token","must-be-rejected"}}
            })}};
            const auto secret_result=client.Post("/v1/connections",auth,secret.dump(),"application/json");
            require(secret_result&&secret_result->status==400,"Connection metadata must reject client-supplied reusable credentials");
            const auto select_nexus=client.Post("/v1/connections/select",auth,
                Json{{"id","company"},{"expected_revision",2}}.dump(),"application/json");
            require(select_nexus&&select_nexus->status==409&&select_nexus->body.find("not authenticated")!=std::string::npos,
                "An unconnected Nexus profile must fail closed before it becomes active");
            catalog=read_catalog(client,auth);
            require(catalog.at("active_id")=="local"&&catalog.at("revision")==2,
                "Failed Nexus selection must leave the active connection unchanged");
            const std::string view_origin="http://127.0.0.1:60420";
            const auto issued=client.Post("/v1/view-sessions",auth,
                Json{{"origin",view_origin}}.dump(),"application/json");
            require(issued&&issued->status==200,"Authenticated local owner must be able to issue a scoped view credential");
            const auto view_credential=Json::parse(issued->body).at("credential").get<std::string>();
            const httplib::Headers view_auth{{"Authorization","View "+view_credential},{"X-XMind-View-Origin",view_origin}};
            const auto view_catalog=client.Get("/v1/connections",view_auth);
            require(view_catalog&&view_catalog->status==200&&Json::parse(view_catalog->body).at("revision")==2,
                "A same-machine view credential must read the connection catalog without the owner token");
            auto view_input=input;view_input["expected_revision"]=2;
            const auto view_update=client.Post("/v1/connections",view_auth,view_input.dump(),"application/json");
            require(view_update&&view_update->status==200&&Json::parse(view_update->body).at("revision")==2,
                ("An origin-bound view may save a validated connection catalog without receiving the owner token (status "+
                    (view_update?std::to_string(view_update->status):std::string("no response"))+", body "+
                    (view_update?view_update->body:std::string{})+")").c_str());
            const httplib::Headers wrong_origin_auth{{"Authorization","View "+view_credential},{"X-XMind-View-Origin","http://127.0.0.1:60421"}};
            const auto wrong_origin=client.Get("/v1/connections",wrong_origin_auth);
            require(wrong_origin&&wrong_origin->status==401,"Connection catalog view credentials must remain origin-bound");
            const auto view_select=client.Post("/v1/connections/select",view_auth,
                Json{{"id","company"},{"expected_revision",2}}.dump(),"application/json");
            require(view_select&&view_select->status==409,"A view may manage the catalog but cannot route execution to an unconnected Nexus server");
            const auto stale=client.Post("/v1/connections/select",auth,
                Json{{"id","local"},{"expected_revision",1}}.dump(),"application/json");
            require(stale&&stale->status==409,"Profile selection must enforce the catalog revision");

            const auto unknown=client.Post("/v1/connections",auth,
                Json{{"expected_revision",2},{"profiles",Json::array({Json{{"id","local"},{"name","Local"},{"kind","local"}}})},{"token","forbidden"}}.dump(),"application/json");
            require(unknown&&unknown->status==400,"Connection update must reject unknown top-level fields");
            const auto unbound=client.Post("/v1/connection-bindings",Json{{"id","local"},{"expected_profile_revision",1}}.dump(),"application/json");require(unbound&&unbound->status==401,"Captured binding requires authentication");
            const auto opened=client.Post("/v1/connection-bindings",view_auth,Json{{"id","local"},{"expected_profile_revision",1}}.dump(),"application/json");require(opened&&opened->status==201,"A view must capture a native Local binding");const auto binding=Json::parse(opened->body);const auto base="/v1/connection-bindings/"+binding.at("binding_id").get<std::string>();const auto identity=binding.at("identity");require(identity.at("connection_id")=="local"&&identity.at("profile_revision")==1,"HTTP binding identity must be captured natively");
            store.create_session("child-history-http","Child history fixture").get();GraphPlan history_plan(R"({"nodes":[{"id":"worker","type":"agent","prompt":"child-owned only"}]})");store.start_graph_run("child-history-parent","child-history-http","history-fixture",1,history_plan,R"({"content":"parent-only"})").get();store.transition("child-history-parent",RunState::queued,RunState::running).get();store.start_graph_child("child-history-child","child-history-parent","worker",R"({"content":"child-owned only"})").get();
            const auto child_history_path=base+"/sessions/child-history-http/runs/child-history-parent/children/child-history-child/history";const auto child_history=client.Post(child_history_path,view_auth,"{}","application/json");require(child_history&&child_history->status==200,"Origin-bound view must read captured native child history");const auto child_receipt=Json::parse(child_history->body);require(child_receipt.at("binding")==identity&&child_receipt.at("session_id")=="child-history-http"&&child_receipt.at("parent_id")=="child-history-parent"&&child_receipt.at("child_id")=="child-history-child"&&child_receipt.at("messages").size()==1&&child_receipt.at("messages")[0].at("data").at("content")=="child-owned only","Captured child history must retain native parent/session linkage and isolated child conversation");const auto wrong_child_session=client.Post(base+"/sessions/foreign/runs/child-history-parent/children/child-history-child/history",view_auth,"{}","application/json");require(wrong_child_session&&wrong_child_session->status==409,"Foreign child session must reject");const auto wrong_child_parent=client.Post(base+"/sessions/child-history-http/runs/child-history-parent/children/child-history-parent/history",view_auth,"{}","application/json");require(wrong_child_parent&&wrong_child_parent->status==404,"Graph root must not be read as its own child");
            const auto other_view=client.Post("/v1/view-sessions",auth,Json{{"origin","http://127.0.0.1:60422"}}.dump(),"application/json");require(other_view&&other_view->status==200,"Second origin-bound view must be available");const httplib::Headers other_auth{{"Authorization","View "+Json::parse(other_view->body).at("credential").get<std::string>()},{"X-XMind-View-Origin","http://127.0.0.1:60422"}};const auto crossed=client.Get(base+"/identity",other_auth);require(crossed&&crossed->status==404,"One view must not acquire another view's HTTP binding handle");
            const auto created=client.Post(base+"/sessions",view_auth,R"({"id":"bound-http","title":"Captured Local session"})","application/json");require(created&&created->status==201&&Json::parse(created->body).at("binding")==identity,"Captured HTTP session creation must use the native binding");require(store.session("bound-http").get().title=="Captured Local session","Captured HTTP operation must persist actual native session state");
            for(const auto* content:{"first","second","third"})store.append_message("bound-http","user",std::string("{\"content\":\"")+content+"\"}").get();
            const auto message_base=base+"/sessions/bound-http/messages";const auto messages=client.Post(message_base,view_auth,R"({"before_sequence":0,"count":2})","application/json");require(messages&&messages->status==200,"Captured HTTP history page must be available to its view");const auto message_page=Json::parse(messages->body);require(message_page.at("binding")==identity&&message_page.at("session_id")=="bound-http"&&message_page.at("messages").size()==2&&message_page.at("messages")[0].at("data").at("content")=="second"&&message_page.at("has_more")==true,"HTTP history must preserve captured identity, chronological order and cursor");
            const auto before_sequence=store.history("bound-http").get()[1].sequence;const auto previous=client.Post(message_base,view_auth,Json{{"before_sequence",before_sequence},{"count",2}}.dump(),"application/json");require(previous&&previous->status==200&&Json::parse(previous->body).at("messages").size()==1&&Json::parse(previous->body).at("messages")[0].at("data").at("content")=="first","Captured HTTP history must page backwards without duplication");
            const auto hidden_messages=client.Post(message_base,other_auth,R"({"before_sequence":0,"count":2})","application/json");require(hidden_messages&&hidden_messages->status==404,"Another view cannot read a captured history handle");const auto invalid_count=client.Post(message_base,view_auth,R"({"before_sequence":0,"count":0})","application/json");require(invalid_count&&invalid_count->status==400,"Captured history bounds must reject invalid page sizes");
            Json options{{"id","captured-model"},{"session_id","bound-http"},{"prompt","Observed request"},{"model_id","selected-model"}};
            auto submit=[&](const char* mode){const auto response=client.Post(base+"/runs",view_auth,options.dump(),"application/json");require(response&&response->status==202,("Captured run admission "+std::string(mode)+" returned "+(response?std::to_string(response->status)+" "+response->body:std::string("no response"))).c_str());require(execution.mode==mode&&execution.observed.model=="selected-model"&&execution.observed.prompt=="Observed request","Native executor must receive the exact selected route/model/prompt");if(execution.observed.provider)require(Json::parse(response->body).at("run").at("provider_context").at("profile_id")=="selected-provider","Captured receipts must preserve public provider context");const auto cancelled=client.Post(base+"/runs/"+options.at("id").get<std::string>()+"/cancel",view_auth,"{}","application/json");require(cancelled&&cancelled->status==200&&store.run(options.at("id").get<std::string>()).get().state==RunState::cancelled,"Settle the synthetic fixture root before the next admission variant");};
            submit("model");options["id"]="captured-provider";options["provider_profile_id"]="selected-provider";options["expected_provider_revision"]=7;submit("provider");require(execution.observed.provider&&execution.observed.provider->id=="selected-provider"&&execution.observed.provider->revision==7,"Provider revision must reach native admission");
            options["id"]="captured-workspace";options["expected_workspace_id"]="fixture-workspace";options["expected_workspace_authority_id"]=std::string(32,'a');options["expected_workspace_revision"]=9;submit("workspace");require(execution.observed.workspace&&execution.observed.workspace->revision==9,"Workspace authority and membership revision must reach native admission");
            options["id"]="captured-files";options["files"]=Json::array({{{"path","input.txt"},{"folder_id","primary"},{"name","input.txt"},{"description","Observed attachment"},{"mention","@input"}}});submit("files");require(execution.observed.files.size()==1&&execution.observed.files[0].folder_id=="primary"&&execution.observed.files[0].description=="Observed attachment","File metadata must reach native admission intact");
            options["id"]="captured-inbox";options["inbox_id"]="queued-input";submit("queued");require(execution.observed.inbox==std::optional<std::string>{"queued-input"}&&execution.observed.files.size()==1&&execution.observed.provider&&execution.observed.workspace,"Queued admission must retain files, provider and workspace");
            auto graph_options=options;graph_options.erase("inbox_id");graph_options["id"]="captured-http-graph";graph_options["prompt"]=std::string(300000,'p');graph_options["graph_id"]="fixture-graph";graph_options["graph_revision"]=7;const auto admitted_graph=client.Post(base+"/graph-runs",view_auth,graph_options.dump(),"application/json");require(admitted_graph&&admitted_graph->status==202&&Json::parse(admitted_graph->body).at("binding")==identity&&Json::parse(admitted_graph->body).at("run").at("graph_root")==true,"View graph admission must retain captured native identity");require(execution.graph_calls==1&&execution.observed_graph.run.prompt.size()==300000&&execution.graph_mode=="files"&&execution.observed_graph.run.files.size()==1&&execution.observed_graph.run.provider&&execution.observed_graph.run.workspace,"Graph HTTP options must preserve native workspace/provider/attachment dispatch");auto rejected_graph=graph_options;rejected_graph["graph_revision"]=6;const auto old_graph=client.Post(base+"/graph-runs",view_auth,rejected_graph.dump(),"application/json");require(old_graph&&old_graph->status==409&&execution.graph_calls==1,"Stale graph revision must reject without dispatch");rejected_graph=graph_options;rejected_graph["plan"]="untrusted";const auto supplied_plan=client.Post(base+"/graph-runs",view_auth,rejected_graph.dump(),"application/json");require(supplied_plan&&supplied_plan->status==400&&execution.graph_calls==1,"Graph clients may not supply executable plans");
            const auto graph_catalog=client.Get(base+"/graphs",view_auth);require(graph_catalog&&graph_catalog->status==200&&Json::parse(graph_catalog->body).at("binding")==identity&&Json::parse(graph_catalog->body).at("graphs")[0].at("id")=="fixture-graph","View graph catalog must retain its captured identity");const auto graph_detail=client.Get(base+"/graph-runs/captured-http-graph",view_auth);require(graph_detail&&graph_detail->status==200&&Json::parse(graph_detail->body).at("binding")==identity&&Json::parse(graph_detail->body).at("graph").at("graph_id")=="fixture-graph"&&Json::parse(graph_detail->body).at("graph").at("context").at("enabled")==false,"View graph detail must retain admitted graph and context");const auto foreign_graph_read=client.Get(base+"/graph-runs/captured-http-graph",other_auth);require(foreign_graph_read&&foreign_graph_read->status==404,"A different origin-bound view cannot borrow a graph binding");
            const auto graph_children=client.Get(base+"/graph-runs/captured-http-graph/children",view_auth);require(graph_children&&graph_children->status==200&&Json::parse(graph_children->body).at("binding")==identity&&Json::parse(graph_children->body).at("root_id")=="captured-http-graph"&&Json::parse(graph_children->body).at("session_id")=="bound-http"&&Json::parse(graph_children->body).at("children").empty(),"Captured graph child reads must preserve binding, graph owner and session");const auto foreign_children=client.Get(base+"/graph-runs/captured-http-graph/children",other_auth);require(foreign_children&&foreign_children->status==404,"Foreign view cannot borrow captured graph child access");
            const auto raw_graph_input=std::string(R"({"answer":1, "answer":2})");const auto graph_control=client.Post(base+"/graph-runs/captured-http-graph/human/answer",view_auth,Json{{"input_json",raw_graph_input},{"expected_checkpoint_revision",1}}.dump(),"application/json");require(graph_control&&graph_control->status==200&&Json::parse(graph_control->body).at("binding")==identity&&execution.controls==1&&execution.control_actor=="local-owner"&&execution.control_input==raw_graph_input,"View graph input must preserve raw bytes and native actor");const auto tree_control=client.Post(base+"/graph-runs/captured-http-graph/invocations/child-owner/human/answer",view_auth,Json{{"input_json",raw_graph_input},{"expected_checkpoint_revision",2},{"expected_owner_checkpoint_revision",3}}.dump(),"application/json");require(tree_control&&tree_control->status==200&&execution.controls==2&&execution.control_owner=="child-owner"&&execution.control_owner_revision==3,"View nested graph input must preserve both checkpoint guards");const auto resume_control=client.Post(base+"/graph-runs/captured-http-graph/resume",view_auth,R"({"expected_checkpoint_revision":4})","application/json");require(resume_control&&resume_control->status==202&&Json::parse(resume_control->body).at("run").at("id")=="captured-http-graph"&&execution.controls==3,"View graph resume must call captured native control");const auto actor_spoof=client.Post(base+"/graph-runs/captured-http-graph/human/answer",view_auth,R"({"input_json":"{}","expected_checkpoint_revision":1,"actor":"foreign"})","application/json");require(actor_spoof&&actor_spoof->status==400&&execution.controls==3,"Graph clients cannot supply authenticated actor");const auto foreign_control=client.Post(base+"/graph-runs/captured-http-graph/resume",other_auth,R"({"expected_checkpoint_revision":4})","application/json");require(foreign_control&&foreign_control->status==404&&execution.controls==3,"A foreign view cannot control another captured graph handle");
            const auto settled_graph=client.Post(base+"/runs/captured-http-graph/cancel",view_auth,"{}","application/json");require(settled_graph&&settled_graph->status==200,"Settle the recorded graph fixture before later session admission");
            auto invalid_options=options;invalid_options["id"]="captured-invalid";invalid_options.erase("expected_workspace_revision");const auto before_calls=execution.calls;const auto rejected_files=client.Post(base+"/runs",view_auth,invalid_options.dump(),"application/json");require(rejected_files&&rejected_files->status==400&&execution.calls==before_calls,"Missing attachment membership revision must reject before native dispatch");invalid_options=options;invalid_options["expected_workspace_authority_id"]=std::string(32,'b');const auto wrong_workspace=client.Post(base+"/runs",view_auth,invalid_options.dump(),"application/json");require(wrong_workspace&&wrong_workspace->status==409&&execution.calls==before_calls,"Changed workspace authority must reject before dispatch");
            store.create_session("ops-http","Operations").get();store.create_run("ops-http-root","ops-http").get();store.transition("ops-http-root",RunState::queued,RunState::running).get();
            const auto expiry=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+600000;
            const std::string exact_arguments=R"({"path":"controlled.txt", "content":"exact bytes"})";
            store.request_operation("ops-http-edit",{"ops-http-root","fixture-workspace","write_file",exact_arguments},expiry).get();
            store.create_session("ops-foreign","Foreign").get();store.create_run("ops-foreign-root","ops-foreign").get();store.transition("ops-foreign-root",RunState::queued,RunState::running).get();store.request_operation("ops-foreign-edit",{"ops-foreign-root","fixture-workspace","write_file","{}"},expiry).get();
            const auto ops_base=base+"/sessions/ops-http/runs/ops-http-root/operations";
            const auto ops=client.Get(ops_base,view_auth);require(ops&&ops->status==200&&Json::parse(ops->body).at("binding")==identity&&Json::parse(ops->body).at("operations").size()==1,"Captured HTTP operations must retain binding and exact run scope");
            store.request_operation("ops-http-read",{"ops-http-root","fixture-workspace","read_file",R"({"path":"controlled.txt"})"},expiry).get();AgentPermissionGrantStore grants(store);const auto policy=agent_permission_policy_binding({},{{"read_file","controlled.txt","ask"}}),grant=agent_permission_grant_identifier("fixture-workspace",policy,"read_file","controlled.txt");grants.record_request({"ops-http-read","ops-http","fixture-workspace",policy,"read_file","controlled.txt",grant});grants.grant("ops-http","ops-http-read");const auto permission_base=base+"/sessions/ops-http";const auto pending=client.Get(permission_base+"/permissions",view_auth);require(pending&&pending->status==200&&Json::parse(pending->body).at("requests").size()==2&&Json::parse(pending->body).at("binding")==identity,"Captured HTTP pending permissions require exact session and binding");auto compatible=Json::parse(pending->body);compatible.erase("binding");require(compatible==Json::parse(client.Get("/v1/sessions/ops-http/permissions",view_auth)->body),"Compatibility and captured permission encoders must agree");const auto saved_grants=client.Get(permission_base+"/permission-grants",view_auth);require(saved_grants&&saved_grants->status==200&&Json::parse(saved_grants->body).at("grants").size()==1,"Captured grants read actual native authority");require(client.Delete(permission_base+"/permission-grants/"+grant,view_auth,R"({"expected_revision":0})","application/json")->status==409,"Stale captured grant revocation refuses");const auto revoked=client.Delete(permission_base+"/permission-grants/"+grant,view_auth,R"({"expected_revision":1})","application/json");require(revoked&&revoked->status==200&&Json::parse(revoked->body).at("revision")==2&&grants.list("ops-http").grants.empty(),"Captured HTTP revocation must remove actual authority");require(client.Get(permission_base+"/permissions",other_auth)->status==404,"Foreign view cannot use captured permission handle");grants.grant("ops-http","ops-http-read");const auto compatibility_revoke=client.Delete("/v1/sessions/ops-http/permission-grants/"+grant,view_auth,R"({"expected_revision":3})","application/json");require(compatibility_revoke&&compatibility_revoke->status==200&&Json::parse(compatibility_revoke->body).at("revision")==4&&grants.list("ops-http").grants.empty(),"Origin-bound views must also revoke through the compatibility route");

            const auto op=client.Get(ops_base+"/ops-http-edit",view_auth);require(op&&op->status==200&&Json::parse(op->body).at("operation").at("arguments_json")==exact_arguments,"HTTP operation inspection must preserve argument bytes");
            const auto no_auth=client.Get(ops_base);require(no_auth&&no_auth->status==401,"Operation scope requires authentication");
            const auto other_ops=client.Get(ops_base,other_auth);require(other_ops&&other_ops->status==404,"Another view cannot inspect a captured operation scope");
            const auto wrong_session=client.Get(base+"/sessions/bound-http/runs/ops-http-root/operations",view_auth);require(wrong_session&&wrong_session->status==409,"Operation scope must validate persisted run session");
            const auto foreign_op=client.Get(ops_base+"/ops-foreign-edit",view_auth);require(foreign_op&&foreign_op->status==409,"Operation inspection must reject another run");
            const auto foreign_decision=client.Post(ops_base+"/ops-foreign-edit/decision",view_auth,R"({"decision":"allow"})","application/json");require(foreign_decision&&foreign_decision->status==409&&store.operation("ops-foreign-edit").get().state==OperationState::awaiting_approval,"Cross-run approval must reject before changing state");
            const auto bad_query=client.Get(ops_base+"?actor=owner",view_auth);require(bad_query&&bad_query->status==400,"Scoped operations reject query parameters");
            const auto bad_actor=client.Post(ops_base+"/ops-http-edit/decision",view_auth,R"({"decision":"allow","actor":"forged"})","application/json");require(bad_actor&&bad_actor->status==400,"Approval actor must remain backend owned");
            const auto bad_decision=client.Post(ops_base+"/ops-http-edit/decision",view_auth,R"({"decision":"unknown"})","application/json");require(bad_decision&&bad_decision->status==400&&store.operation("ops-http-edit").get().state==OperationState::awaiting_approval,"Malformed decision must cause no state change");
            const auto allowed=client.Post(ops_base+"/ops-http-edit/decision",view_auth,R"({"decision":"allow"})","application/json");require(allowed&&allowed->status==200&&Json::parse(allowed->body).at("binding")==identity&&Json::parse(allowed->body).at("operation").at("decision_actor")=="local-owner"&&store.operation("ops-http-edit").get().state==OperationState::ready,"Captured native decision must record backend actor and ready state");
            const auto repeated=client.Post(ops_base+"/ops-http-edit/decision",view_auth,R"({"decision":"deny"})","application/json");require(repeated&&repeated->status==409&&store.operation("ops-http-edit").get().state==OperationState::ready,"Repeated approval cannot overwrite a committed decision");
            store.create_run("bound-root","bound-http").get();store.append_event("bound-root","fixture.event","{}").get();const auto run=client.Get(base+"/runs/bound-root",view_auth);require(run&&run->status==200&&Json::parse(run->body).at("run").at("state")=="queued","Captured HTTP run inspection must use persisted native state");
            const auto event_input=Json{{"session_id","bound-http"},{"run_id","bound-root"},{"cursor",nullptr}};const auto events=client.Post(base+"/events",view_auth,event_input.dump(),"application/json");require(events&&events->status==200&&!Json::parse(events->body).at("events").empty(),"Captured HTTP event page must read actual native events");auto resumed=event_input;resumed["cursor"]=Json::parse(events->body).at("cursor");auto invalid=resumed;invalid["cursor"]["session_id"]="another-session";const auto wrong_cursor=client.Post(base+"/events",view_auth,invalid.dump(),"application/json");require(wrong_cursor&&wrong_cursor->status==409,"Event cursors must remain scoped to their captured session");invalid=resumed;invalid["cursor"]["sequence"]=9007199254740991LL;const auto ahead=client.Post(base+"/events",view_auth,invalid.dump(),"application/json");require(ahead&&ahead->status==409,"An uncommitted cursor boundary must not be accepted");
            auto renamed=input;renamed["expected_revision"]=2;renamed["profiles"][0]["name"]="Next Local";const auto edited=client.Post("/v1/connections",auth,renamed.dump(),"application/json");require(edited&&edited->status==200,"Local profile edit must remain available");const auto stale_admission=client.Post(base+"/sessions",view_auth,R"({"id":"stale-binding-session","title":"must not exist"})","application/json");require(stale_admission&&stale_admission->status==409,"Stale HTTP bindings must not admit new sessions");const auto retained=client.Get(base+"/sessions/bound-http",view_auth);require(retained&&retained->status==200&&Json::parse(retained->body).at("binding")==identity,"Profile edits must preserve original captured reads and identity");const auto next_events=client.Post(base+"/events",view_auth,resumed.dump(),"application/json");require(next_events&&next_events->status==200,"Captured event reads must survive a metadata edit");
            const auto retained_graph=client.Get(base+"/graph-runs/captured-http-graph",view_auth);require(retained_graph&&retained_graph->status==200&&Json::parse(retained_graph->body).at("binding")==identity,"Profile edits preserve captured graph detail identity");
            const auto retained_messages=client.Post(message_base,view_auth,R"({"before_sequence":0,"count":2})","application/json");require(retained_messages&&retained_messages->status==200&&Json::parse(retained_messages->body).at("binding")==identity,"Metadata changes preserve captured history reads");
            const auto retained_op=client.Get(ops_base+"/ops-http-edit",view_auth);require(retained_op&&retained_op->status==200&&Json::parse(retained_op->body).at("binding")==identity,"Operation inspection must retain captured identity after metadata edits");
            const auto closed=client.Delete(base,view_auth);require(closed&&closed->status==200,"A view must be able to close its own captured handle");const auto after_close=client.Get(base+"/identity",view_auth);require(after_close&&after_close->status==404&&store.run("bound-root").get().state==RunState::queued,"Closing the view binding must not cancel admitted work");
            const auto closed_ops=client.Get(ops_base,view_auth);require(closed_ops&&closed_ops->status==404,"Closed binding must fence operation access");
            saved=read_catalog(client,auth);
            }
            store.close();
        }
        {
            PersistenceService reopened(database.path.string(),{argv[1],argv[2]});
            {
                Access access(reopened,token);
                httplib::Client client("127.0.0.1",access.port);
                client.set_connection_timeout(2,0);client.set_read_timeout(3,0);
                const auto restored=read_catalog(client,auth);
                require(restored==saved,"Connection profiles and active selection must survive SQLite reopen");
            }
            reopened.close();
        }
        std::cout<<"Native HTTP connection profile contract passed owner authentication, origin-bound view access, Local initialization, Nexus configuration disclosure, credential rejection, fail-closed remote selection, revision checks and SQLite restart persistence. Nexus authentication and command/event routing remain unimplemented.\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
