#include "connection_run_recording_fixture.hpp"
#include "agentflow/http_server.hpp"
#include "agentflow/connection_binding.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
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
using test::RecordingExecutor;
struct Access {
    HttpServer server;
    int port;
    std::thread worker;
    Access(PersistenceService& store,const std::string& token,RunExecutor* executor=nullptr)
        :server(store,token,executor),port(server.bind(0)),worker([this]{server.listen();}){
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
            RecordingExecutor execution(store);Access access(store,token,&execution);
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
            const auto other_view=client.Post("/v1/view-sessions",auth,Json{{"origin","http://127.0.0.1:60422"}}.dump(),"application/json");require(other_view&&other_view->status==200,"Second origin-bound view must be available");const httplib::Headers other_auth{{"Authorization","View "+Json::parse(other_view->body).at("credential").get<std::string>()},{"X-XMind-View-Origin","http://127.0.0.1:60422"}};const auto crossed=client.Get(base+"/identity",other_auth);require(crossed&&crossed->status==404,"One view must not acquire another view's HTTP binding handle");
            const auto created=client.Post(base+"/sessions",view_auth,R"({"id":"bound-http","title":"Captured Local session"})","application/json");require(created&&created->status==201&&Json::parse(created->body).at("binding")==identity,"Captured HTTP session creation must use the native binding");require(store.session("bound-http").get().title=="Captured Local session","Captured HTTP operation must persist actual native session state");
            Json options{{"id","captured-model"},{"session_id","bound-http"},{"prompt","Observed request"},{"model_id","selected-model"}};
            auto submit=[&](const char* mode){const auto response=client.Post(base+"/runs",view_auth,options.dump(),"application/json");require(response&&response->status==202,("Captured run admission "+std::string(mode)+" returned "+(response?std::to_string(response->status)+" "+response->body:std::string("no response"))).c_str());require(execution.mode==mode&&execution.observed.model=="selected-model"&&execution.observed.prompt=="Observed request","Native executor must receive the exact selected route/model/prompt");if(execution.observed.provider)require(Json::parse(response->body).at("run").at("provider_context").at("profile_id")=="selected-provider","Captured receipts must preserve public provider context");const auto cancelled=client.Post(base+"/runs/"+options.at("id").get<std::string>()+"/cancel",view_auth,"{}","application/json");require(cancelled&&cancelled->status==200&&store.run(options.at("id").get<std::string>()).get().state==RunState::cancelled,"Settle the synthetic fixture root before the next admission variant");};
            submit("model");options["id"]="captured-provider";options["provider_profile_id"]="selected-provider";options["expected_provider_revision"]=7;submit("provider");require(execution.observed.provider&&execution.observed.provider->id=="selected-provider"&&execution.observed.provider->revision==7,"Provider revision must reach native admission");
            options["id"]="captured-workspace";options["expected_workspace_id"]="fixture-workspace";options["expected_workspace_authority_id"]=std::string(32,'a');options["expected_workspace_revision"]=9;submit("workspace");require(execution.observed.workspace&&execution.observed.workspace->revision==9,"Workspace authority and membership revision must reach native admission");
            options["id"]="captured-files";options["files"]=Json::array({{{"path","input.txt"},{"folder_id","primary"},{"name","input.txt"},{"description","Observed attachment"},{"mention","@input"}}});submit("files");require(execution.observed.files.size()==1&&execution.observed.files[0].folder_id=="primary"&&execution.observed.files[0].description=="Observed attachment","File metadata must reach native admission intact");
            options["id"]="captured-inbox";options["inbox_id"]="queued-input";submit("queued");require(execution.observed.inbox==std::optional<std::string>{"queued-input"}&&execution.observed.files.size()==1&&execution.observed.provider&&execution.observed.workspace,"Queued admission must retain files, provider and workspace");
            auto invalid_options=options;invalid_options["id"]="captured-invalid";invalid_options.erase("expected_workspace_revision");const auto before_calls=execution.calls;const auto rejected_files=client.Post(base+"/runs",view_auth,invalid_options.dump(),"application/json");require(rejected_files&&rejected_files->status==400&&execution.calls==before_calls,"Missing attachment membership revision must reject before native dispatch");invalid_options=options;invalid_options["expected_workspace_authority_id"]=std::string(32,'b');const auto wrong_workspace=client.Post(base+"/runs",view_auth,invalid_options.dump(),"application/json");require(wrong_workspace&&wrong_workspace->status==409&&execution.calls==before_calls,"Changed workspace authority must reject before dispatch");
            store.create_run("bound-root","bound-http").get();store.append_event("bound-root","fixture.event","{}").get();const auto run=client.Get(base+"/runs/bound-root",view_auth);require(run&&run->status==200&&Json::parse(run->body).at("run").at("state")=="queued","Captured HTTP run inspection must use persisted native state");
            const auto event_input=Json{{"session_id","bound-http"},{"run_id","bound-root"},{"cursor",nullptr}};const auto events=client.Post(base+"/events",view_auth,event_input.dump(),"application/json");require(events&&events->status==200&&!Json::parse(events->body).at("events").empty(),"Captured HTTP event page must read actual native events");auto resumed=event_input;resumed["cursor"]=Json::parse(events->body).at("cursor");auto invalid=resumed;invalid["cursor"]["session_id"]="another-session";const auto wrong_cursor=client.Post(base+"/events",view_auth,invalid.dump(),"application/json");require(wrong_cursor&&wrong_cursor->status==409,"Event cursors must remain scoped to their captured session");invalid=resumed;invalid["cursor"]["sequence"]=9007199254740991LL;const auto ahead=client.Post(base+"/events",view_auth,invalid.dump(),"application/json");require(ahead&&ahead->status==409,"An uncommitted cursor boundary must not be accepted");
            auto renamed=input;renamed["expected_revision"]=2;renamed["profiles"][0]["name"]="Next Local";const auto edited=client.Post("/v1/connections",auth,renamed.dump(),"application/json");require(edited&&edited->status==200,"Local profile edit must remain available");const auto stale_admission=client.Post(base+"/sessions",view_auth,R"({"id":"stale-binding-session","title":"must not exist"})","application/json");require(stale_admission&&stale_admission->status==409,"Stale HTTP bindings must not admit new sessions");const auto retained=client.Get(base+"/sessions/bound-http",view_auth);require(retained&&retained->status==200&&Json::parse(retained->body).at("binding")==identity,"Profile edits must preserve original captured reads and identity");const auto next_events=client.Post(base+"/events",view_auth,resumed.dump(),"application/json");require(next_events&&next_events->status==200,"Captured event reads must survive a metadata edit");
            const auto closed=client.Delete(base,view_auth);require(closed&&closed->status==200,"A view must be able to close its own captured handle");const auto after_close=client.Get(base+"/identity",view_auth);require(after_close&&after_close->status==404&&store.run("bound-root").get().state==RunState::queued,"Closing the view binding must not cancel admitted work");
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
