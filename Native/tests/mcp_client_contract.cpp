#include "agentflow/mcp_client.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
#include <thread>
using namespace agentflow;
using namespace std::chrono_literals;
namespace {
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
McpStdioConfiguration configuration(char** argv,const std::string& mode){return {argv[1],argv[3],{argv[2],mode},{}};}
template<class Action> void rejected(Action action){try{action();}catch(const McpProtocolError&){return;}throw std::runtime_error("Retired MCP owner accepted work");}
void success(char** argv,const std::string& mode) {
    McpStdioClient client(configuration(argv,mode));require(!client.ready(),"Construction cannot establish negotiation readiness");
    client.connect(std::chrono::steady_clock::now()+5s);require(client.ready(),"Actual negotiated client must be ready");
    require(client.server().era==(mode=="legacy"?McpWireEra::legacy:McpWireEra::modern),"Client must retain the negotiated era");
    try{client.list_tools(std::string(4097,'x'),std::chrono::steady_clock::now()+5s);throw std::runtime_error("Oversized cursor accepted");}catch(const std::invalid_argument&){}
    require(client.ready(),"Caller cursor rejection must not retire a healthy connection");
    const auto page=client.list_tools({},std::chrono::steady_clock::now()+5s);
    require(page.tools.size()==1 && page.tools[0].name=="fixture.read","Actual pipe catalogue must reach the native owner");
    require(nlohmann::json::parse(page.tools[0].input_schema_json)["required"][0]=="path","Input schema must remain available for future validation");
    require(nlohmann::json::parse(page.tools[0].annotations_json)["readOnlyHint"]==true,"Untrusted annotation is descriptive data only");
    if(mode=="modern"){
        require(page.next_cursor=="opaque-page-2","Opaque cursor must remain exact");
        const auto last=client.list_tools(page.next_cursor,std::chrono::steady_clock::now()+5s);require(last.tools.empty() && !last.next_cursor,"Cursor must be sent to the actual peer");
    }
    client.shutdown();require(!client.ready(),"Closed client cannot remain ready");
    require(client.status().exit_code==0,"Catalogue peer must accept the actual request/response sequence and shutdown");
    rejected([&]{client.connect(std::chrono::steady_clock::now()+5s);});
    rejected([&]{client.list_tools({},std::chrono::steady_clock::now()+5s);});
}
void failure(char** argv,const std::string& mode) {
    McpStdioClient client(configuration(argv,mode));client.connect(std::chrono::steady_clock::now()+5s);
    bool failed=false;try{client.list_tools({},std::chrono::steady_clock::now()+(mode=="timeout"?200ms:5s));}catch(const McpProtocolError&){failed=true;}catch(const McpTransportError&){failed=true;}
    require(failed && !client.ready(),"Invalid/interrupted discovery must retire the real owned connection");
    require(client.status().exit_code==0,"Failure fixture must verify its expected wire sequence and exit successfully");
    rejected([&]{client.list_tools({},std::chrono::steady_clock::now()+5s);});
    rejected([&]{client.connect(std::chrono::steady_clock::now()+5s);});
}
void cancellation(char** argv) {
    McpStdioClient client(configuration(argv,"cancel"));client.connect(std::chrono::steady_clock::now()+5s);
    std::stop_source cancel;std::jthread request([&]{std::this_thread::sleep_for(100ms);cancel.request_stop();});
    const auto start=std::chrono::steady_clock::now();bool stopped=false;
    try{client.list_tools({},start+5s,cancel.get_token());}catch(const McpTransportCancelled&){stopped=true;}
    require(stopped && !client.ready() && std::chrono::steady_clock::now()-start<2s,"Actual waiting catalogue read must cancel and retire promptly");
    require(client.status().exit_code==0,"Cancelled peer must observe cancellation and clean EOF");
}
}
int main(int argc,char** argv){
    if(argc!=4)return 2;
    try {
        success(argv,"modern");success(argv,"legacy");
        for(const auto* mode:{"duplicate","bad-schema","unknown-id","modern-request","modern-input-required","flood","closed","timeout"})failure(argv,mode);
        cancellation(argv);
        std::cout<<"Native MCP stdio owner passed independent real subprocess catalogues, pagination, legacy ping/unsupported-method handling, bounded notifications, malformed results, cancellation/timeout/EOF and no reconnect/replay. No tool execution, schema validation or full MCP integration claimed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
