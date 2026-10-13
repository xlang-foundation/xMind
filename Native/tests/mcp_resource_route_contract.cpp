#include "agentflow/http_server.hpp"
#include "agentflow/mcp_resource_catalog.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include <iostream>
#include <thread>

using namespace agentflow;
using Json=nlohmann::json;
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
class FixtureCatalog final:public McpResourceCatalog {
public:
    std::string list_json()override{return R"({"servers":[{"id":"peer","state":"connected"}],"resources":[{"server_id":"peer","resource":{"name":"readme","uri":"fixture://readme","description":"fixture","mimeType":"text/plain","annotations":{"private":"drop"}}}],"templates":[{"server_id":"peer","template":{"name":"record","uriTemplate":"fixture://records/{id}","description":"record template","mimeType":"application/json","annotations":{"private":"drop"}}}]})";}
};
}
int main(int argc,char** argv){
    if(argc!=4)return 2;
    try{
        PersistenceService persistence(argv[1],{argv[2],argv[3]});FixtureCatalog catalog;
        const std::string token="synthetic-mcp-resource-route-contract-token-0001";
        HttpServer server(persistence,token,nullptr,nullptr,{}, {},{},nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,&catalog);
        const auto port=server.bind(0);if(port<=0)throw std::runtime_error("MCP resource route server failed to bind");std::jthread listener([&]{server.listen();});
        httplib::Client client("127.0.0.1",port);client.set_connection_timeout(2,0);client.set_read_timeout(5,0);const httplib::Headers auth{{"Authorization","Bearer "+token}};
        auto response=client.Get("/v1/mcp/resource",auth);require(response&&response->status==200,"Default MCP resource location must be readable");
        auto result=Json::parse(response->body);require(result.size()==2&&result.contains("location")&&result.contains("data"),"Response envelope must contain exactly location and data");
        require(result["location"].size()==1&&result["location"].contains("directory")&&result["location"]["directory"].is_string(),"Default location must identify an absolute directory");
        require(result["data"].size()==2&&result["data"]["resources"].size()==1&&result["data"]["templates"].size()==1,"MCP resource and template catalogues must be present");
        const auto& resource=result["data"]["resources"][0];require(resource.size()==5&&resource["server"]=="peer"&&resource["name"]=="readme"&&resource["uri"]=="fixture://readme"&&resource["description"]=="fixture"&&resource["mimeType"]=="text/plain","Resource projection must match the public schema and omit native-only fields");
        const auto& resource_template=result["data"]["templates"][0];require(resource_template.size()==5&&resource_template["server"]=="peer"&&resource_template["name"]=="record"&&resource_template["uriTemplate"]=="fixture://records/{id}"&&resource_template["description"]=="record template"&&resource_template["mimeType"]=="application/json","Template projection must match the public schema and omit native-only fields");
        response=client.Get("/v1/mcp/resource?location%5Bdirectory%5D=C%3A%5C%5Cprojects%5C%5C..%5C%5Cworkspace",auth);require(response&&response->status==200,"Absolute location query must be accepted");result=Json::parse(response->body);require(result["location"]["directory"]=="C:\\workspace","Location paths must be normalized before returning them");
        for(const auto* path:{"/v1/mcp/resource?unknown=value","/v1/mcp/resource?location%5Bdirectory%5D=relative","/v1/mcp/resource?location%5Bdirectory%5D=C%3A%5C%5Cone&location%5Bdirectory%5D=C%3A%5C%5Ctwo"}){response=client.Get(path,auth);require(response&&response->status==400,"Unknown, relative or duplicate location queries must be rejected");}
        response=client.Get("/v1/mcp/resource",httplib::Headers{{"Authorization","Bearer wrong-token"}});require(response&&response->status==401,"MCP resource catalogue must require native bearer authentication");
        server.stop();listener.join();persistence.close();std::cout<<"Native MCP resource route contract passed schema projection, location normalization, strict query validation and bearer authentication.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
