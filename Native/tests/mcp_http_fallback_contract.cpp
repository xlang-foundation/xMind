#include "agentflow/mcp_http_client.hpp"
#include <iostream>
using namespace agentflow;
using namespace std::chrono_literals;
int main(int argc,char** argv){
    if(argc!=3)return 2;
    try{
        McpHttpClient client(argv[1]);bool rejected=false;
        try{client.connect(std::chrono::steady_clock::now()+5s);}catch(const McpProtocolError&){rejected=true;}
        if(std::string(argv[2])=="fallback"){
            if(rejected||!client.ready()||client.server().era!=McpWireEra::legacy||client.server().protocol_version!="2025-11-25")throw std::runtime_error("Unrecognized discovery response must fall back to legacy initialization");
        }else if(!rejected||client.ready())throw std::runtime_error("Recognized modern discovery errors must never downgrade");
        client.shutdown();
        std::cout<<"MCP HTTP discovery fallback passed for "<<argv[2]<<"\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
