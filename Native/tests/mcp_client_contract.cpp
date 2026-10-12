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
    const auto resources=client.list_resources({},std::chrono::steady_clock::now()+5s);
    require(resources.entries_json.size()==1 && nlohmann::json::parse(resources.entries_json[0])["uri"]=="file:///fixture/readme.md","Actual peer resource catalogue must reach the native owner");
    require(resources.next_cursor=="resource-page-2","Resource pagination cursor must remain exact");
    const auto last_resources=client.list_resources(resources.next_cursor,std::chrono::steady_clock::now()+5s);
    require(last_resources.entries_json.empty()&&!last_resources.next_cursor,"Resource cursor must be sent to the peer");
    const auto resource=client.read_resource("file:///fixture/readme.md",std::chrono::steady_clock::now()+5s);
    require(!resource.request_id.empty()&&nlohmann::json::parse(resource.result_json)["contents"][0]["text"]=="fixture resource body","Actual peer resource contents and correlated request identity must be retained");
    const auto prompts=client.list_prompts({},std::chrono::steady_clock::now()+5s);
    require(prompts.entries_json.size()==1&&nlohmann::json::parse(prompts.entries_json[0])["arguments"][0]["name"]=="topic","Actual peer prompt definitions must reach the native owner");
    const auto prompt=client.get_prompt("fixture.prompt",R"({"topic":"AgentFlow"})",std::chrono::steady_clock::now()+5s);
    require(!prompt.request_id.empty()&&nlohmann::json::parse(prompt.result_json)["messages"][0]["content"]["text"]=="Discuss AgentFlow","Actual peer prompt messages and correlated request identity must be retained");
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
void subscription(char** argv) {
    McpStdioClient client(configuration(argv,"subscription"));client.connect(std::chrono::steady_clock::now()+5s);
    McpSubscriptionFilter requested;requested.tools_list_changed=true;requested.resource_subscriptions={"file:///fixture/readme.md"};
    std::vector<std::string> events;bool acknowledged=false;
    const auto accepted=client.listen_subscriptions(requested,[&](const McpWireMessage& message){events.push_back(message.method);},std::chrono::steady_clock::now()+5s,{},
        [&](const McpSubscriptionAcknowledgement& value){acknowledged=true;require(value.notifications.tools_list_changed,"Validated stdio acknowledgement must reach its immediate callback");events.push_back("acknowledged");});
    require(accepted.subscription_id.size()>0&&accepted.notifications.tools_list_changed,"Stdio subscription acknowledgement must be retained");
    require(accepted.notifications.resource_subscriptions==requested.resource_subscriptions,"Stdio subscription resource filters must be acknowledged exactly");
    require(acknowledged&&events==std::vector<std::string>{"acknowledged","notifications/resources/updated","notifications/tools/list_changed"},"Validated acknowledgement must reach its callback before post-ack events");
    client.shutdown();require(client.status().exit_code==0,"Subscription peer must verify completion and exit cleanly");
}
void subscription_cancellation(char** argv) {
    McpStdioClient client(configuration(argv,"subscription-cancel"));client.connect(std::chrono::steady_clock::now()+5s);
    McpSubscriptionFilter requested;requested.tools_list_changed=true;
    std::stop_source cancel;std::jthread request([&]{std::this_thread::sleep_for(100ms);cancel.request_stop();});
    bool stopped=false;try{(void)client.listen_subscriptions(requested,{},std::chrono::steady_clock::now()+5s,cancel.get_token());}catch(const McpTransportCancelled&){stopped=true;}
    require(stopped&&!client.ready(),"Cancelling stdio subscriptions retires the stream owner without inventing completion");
    require(client.status().exit_code==0,"Subscription peer must observe cancellation and clean EOF");
}
void subscription_rejection(char** argv,const std::string& mode) {
    McpStdioClient client(configuration(argv,mode));client.connect(std::chrono::steady_clock::now()+5s);
    McpSubscriptionFilter requested;requested.tools_list_changed=true;
    bool failed=false;try{(void)client.listen_subscriptions(requested,{},std::chrono::steady_clock::now()+5s);}catch(const McpProtocolError&){failed=true;}
    require(failed&&!client.ready(),"Unrequested MCP subscription acknowledgement/event must retire the stdio owner");
    require(client.status().exit_code==0,"Rejected subscription peer must observe cancellation and clean EOF");
}
}
int main(int argc,char** argv){
    if(argc!=4)return 2;
    try {
        success(argv,"modern");success(argv,"legacy");
        for(const auto* mode:{"duplicate","bad-schema","unknown-id","modern-request","modern-input-required","flood","closed","timeout"})failure(argv,mode);
        cancellation(argv);subscription(argv);subscription_cancellation(argv);
        subscription_rejection(argv,"subscription-bad-ack");subscription_rejection(argv,"subscription-bad-event");
        std::cout<<"Native MCP stdio owner passed independent real subprocess catalogues, pagination, legacy ping/unsupported-method handling, bounded notifications, malformed results, cancellation/timeout/EOF and no reconnect/replay. No tool execution, schema validation or full MCP integration claimed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
