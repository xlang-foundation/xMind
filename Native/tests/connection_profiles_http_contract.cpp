#include "agentflow/http_server.hpp"
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
struct Access {
    HttpServer server;
    int port;
    std::thread worker;
    Access(PersistenceService& store,const std::string& token)
        :server(store,token),port(server.bind(0)),worker([this]{server.listen();}){
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
            Access access(store,token);
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
