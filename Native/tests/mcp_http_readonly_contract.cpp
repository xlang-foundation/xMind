#include "agentflow/mcp_http_client.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
using namespace agentflow;
using namespace std::chrono_literals;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    try{
        McpHttpClient client(argv[1]);const auto deadline=std::chrono::steady_clock::now()+10s;
        client.connect(deadline);require(client.ready()&&client.server().era==McpWireEra::modern,"HTTP client must complete the actual modern handshake");
        const auto capabilities=nlohmann::json::parse(client.server().capabilities_json);require(capabilities.contains("resources")&&capabilities.contains("prompts"),"HTTP peer must advertise both read-only capabilities");
        const auto resources=client.list_resources({},deadline);require(resources.entries_json.size()==1&&nlohmann::json::parse(resources.entries_json[0])["uri"]=="fixture://http/readme"&&resources.next_cursor=="http-resource-page-2","HTTP resource catalogue and opaque pagination cursor must be retained");
        const auto last=client.list_resources(resources.next_cursor,deadline);require(last.entries_json.empty()&&!last.next_cursor,"HTTP client must send the resource cursor to the peer");
        const auto resource=client.read_resource("fixture://http/readme",deadline);require(!resource.request_id.empty()&&nlohmann::json::parse(resource.result_json)["contents"][0]["text"]=="HTTP resource body","HTTP resource result must retain validated content and correlation identity");
        const auto prompts=client.list_prompts({},deadline);require(prompts.entries_json.size()==1&&nlohmann::json::parse(prompts.entries_json[0])["name"]=="fixture.http.prompt","HTTP prompt catalogue must reach the native owner");
        const auto prompt=client.get_prompt("fixture.http.prompt",R"({"topic":"AgentFlow"})",deadline);require(!prompt.request_id.empty()&&nlohmann::json::parse(prompt.result_json)["messages"][0]["content"]["text"]=="HTTP prompt for AgentFlow","HTTP prompt result must retain its exact message and correlation identity");
        client.shutdown();require(!client.ready(),"Closed HTTP client cannot remain ready");
        std::cout<<"Native MCP Streamable HTTP resources/prompts passed handshake, pagination, correlated reads and prompt retrieval. No external service or credential was used.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
