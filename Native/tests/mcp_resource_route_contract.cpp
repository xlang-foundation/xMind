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
        auto response=client.Get("/v1/mcp/resources",auth);require(response&&response->status==200,"Native MCP resource catalog must be readable");
        const auto result=Json::parse(response->body);require(result.size()==3&&result.contains("servers")&&result.contains("resources")&&result.contains("templates"),"Native catalog must keep its own envelope");
        require(result["servers"].size()==1&&result["servers"][0]["id"]=="peer","Native catalog must preserve connected server metadata");
        require(result["resources"].size()==1&&result["resources"][0]["server_id"]=="peer"&&result["resources"][0]["resource"]["uri"]=="fixture://readme","Native catalog must preserve its server-scoped resource records");
        require(result["templates"].size()==1&&result["templates"][0]["server_id"]=="peer"&&result["templates"][0]["template"]["uriTemplate"]=="fixture://records/{id}","Native catalog must preserve its server-scoped templates");
        response=client.Get("/v1/mcp/resources?cursor=client-supplied",auth);require(response&&response->status==400,"Native MCP pagination cannot be selected by a client");
        response=client.Get("/v1/mcp/resource",auth);require(response&&response->status==404,"Foreign-shaped MCP resource routes are not part of the xMind API");
        response=client.Get("/v1/mcp/resources",httplib::Headers{{"Authorization","Bearer wrong-token"}});require(response&&response->status==401,"Native MCP resource catalog must require bearer authentication");
        server.stop();listener.join();persistence.close();std::cout<<"Native MCP catalog contract passed native schema, query rejection, foreign-route rejection and bearer authentication.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
