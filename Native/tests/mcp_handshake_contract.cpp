#include "agentflow/mcp_handshake.hpp"
#include "agentflow/mcp_stdio.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
using namespace agentflow;
using namespace std::chrono_literals;
namespace {
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
void negotiate(char** argv,const std::string& mode,bool expected_failure=false) {
    McpStdioProcess process({argv[1],argv[3],{argv[2],mode},{}});
    McpRequestTracker requests;McpHandshake handshake(requests);
    std::vector<McpHandshakeAction> actions;std::vector<std::string> peer_replies;std::optional<McpCorrelatedReply> ordinary;
    McpLineStream wire([&](const auto& message){
        if(message.kind==McpMessageKind::request){require(handshake.ready(),"Peer requests cannot bypass negotiation");peer_replies.push_back(mcp_peer_reply(message,handshake.server().era));return;}
        require(message.kind==McpMessageKind::result || message.kind==McpMessageKind::error,"Fixture lifecycle expects RPC responses");
        const auto reply=requests.receive(message);if(!reply)return;
        if(handshake.ready())ordinary=reply;else actions.push_back(handshake.accept(*reply));
    });
    auto current=handshake.begin();require(!handshake.ready(),"Preparing discovery cannot establish readiness");
    auto deadline=std::chrono::steady_clock::now()+10s;process.write(current.frame,deadline);
    bool fallback=false,failed=false;
    try {
        while(!handshake.ready()){
            actions.clear();
            try {
                const auto bytes=process.read(mode=="legacy-timeout" && !fallback?std::chrono::steady_clock::now()+150ms:deadline);
                require(bool(bytes),"Lifecycle fixture exited before negotiation");wire.feed(*bytes);
            }catch(const McpTransportTimeout&){require(mode=="legacy-timeout" && !fallback,"Only the intended probe timeout may trigger fallback");fallback=true;current=handshake.probe_timeout();process.write(current.frame,deadline);continue;}
            for(auto& action:actions){
                if(action.request){require(!handshake.ready(),"Initialization request is not readiness");process.write(action.request->frame,deadline);}
                if(!action.notification.empty()){require(!handshake.ready(),"Legacy readiness must wait for its actual notification write");process.write(action.notification,deadline);handshake.initialized_sent();}
            }
        }
    }catch(const McpProtocolError&){failed=true;}
    require(failed==expected_failure,"Unexpected native negotiation result");
    if(!failed){
        const auto& server=handshake.server();const bool legacy=mode.starts_with("legacy");
        require(server.era==(legacy?McpWireEra::legacy:McpWireEra::modern),"Negotiated protocol era must match the actual peer");
        require(server.protocol_version==(legacy?(mode=="legacy-older"?"2025-06-18":mode=="legacy-oldest"?"2024-11-05":"2025-11-25"):"2026-07-28"),"Negotiated version must be supported");
        require(nlohmann::json::parse(server.capabilities_json).contains("tools"),"Actual peer capabilities must be retained");
        const auto listed=requests.prepare("tools/list","{}",server.era);process.write(listed.frame,deadline);
        while(!ordinary){const auto bytes=process.read(deadline);require(bool(bytes),"Peer exited before the post-negotiation request");wire.feed(*bytes);for(const auto& frame:peer_replies)process.write(frame,deadline);peer_replies.clear();}
        require(ordinary->request.id==listed.request.id && nlohmann::json::parse(ordinary->response.payload_json)["tools"].empty(),"Actual post-negotiation request must correlate");
    }else{require(!handshake.ready(),"Rejected negotiation cannot expose a ready server");try{handshake.server();throw std::runtime_error("Failed server was exposed");}catch(const McpProtocolError&){} }
    process.shutdown();require(process.status().exit_code==0,"Independent peer must see a valid method/ack sequence and exit cleanly");
}
}
int main(int argc,char** argv){
    if(argc!=4)return 2;
    try {
        for(const auto* mode:{"modern","legacy","legacy-invalid-params","legacy-timeout","legacy-older","legacy-oldest"})negotiate(argv,mode);
        for(const auto* mode:{"modern-unsupported","modern-capability-error","modern-no-version","legacy-rejected","legacy-unsupported"})negotiate(argv,mode,true);
        std::cout<<"Native MCP negotiation passed actual independent stdio peers: modern discovery, legacy error/timeout fallback, retired late probe, acknowledgement ordering, post-connect metadata and unsupported/error rejection. No model/tool effect or full MCP integration claimed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
