#include "agentflow/mcp_stdio.hpp"
#include "agentflow/mcp_requests.hpp"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <thread>
#include <cstdlib>
using namespace agentflow;
using namespace std::chrono_literals;
namespace {
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class Error,class Action> void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected transport failure did not occur");}
McpStdioConfiguration config(char** argv,const std::string& mode){return {argv[1],argv[3],{argv[2],mode},{{"XMIND_CHILD_ONLY","configured-fixture"}}};}
void fixture_ready(McpStdioProcess& process) {
    bool observed=false;McpLineStream stream([&](const auto& message){require(message.kind==McpMessageKind::notification && message.method=="fixture/ready","Independent fixture readiness must be an actual notification");observed=true;});
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(!observed){const auto bytes=process.read(deadline);require(bool(bytes),"Fixture exited before readiness");stream.feed(*bytes);}
}
nlohmann::json discover(McpStdioProcess& process) {
    McpRequestTracker requests;const auto request=requests.prepare("server/discover","{}",McpWireEra::modern);
    std::optional<McpCorrelatedReply> reply;McpLineStream stream([&](const auto& message){reply=requests.receive(message);});
    const auto deadline=std::chrono::steady_clock::now()+10s;process.write(request.frame,deadline);
    while(!reply){const auto bytes=process.read(deadline);require(bool(bytes),"Peer exited before its discovery response");stream.feed(*bytes);}
    require(reply->response.kind==McpMessageKind::result,"Peer discovery must produce an actual RPC result");return nlohmann::json::parse(reply->response.payload_json);
}
}
int main(int argc,char** argv) {
    if(argc!=4)return 2;
    try {
        _putenv_s("XMIND_AUTH_TOKEN","fixture-parent-access-secret");_putenv_s("XMIND_API_KEY","fixture-parent-provider-secret");_putenv_s("XMIND_PARENT_ONLY","fixture-parent-only");
        {
            auto launch=config(argv,"normal");launch.arguments.insert(launch.arguments.end(),{"argument with spaces","\"quoted\"","trailing\\","你好"});
            McpStdioProcess process(launch);const auto result=discover(process);
            require(result["fixtureText"]=="你好 🌍\nfixture" && result["childConfigured"]==true,"Actual pipe/UTF-8 and configured environment must survive");
            require(result["arguments"]==nlohmann::json::array({"argument with spaces","\"quoted\"","trailing\\","你好"}),"Native argument quoting must preserve exact arguments");
            const auto drain=std::chrono::steady_clock::now()+5s;while(process.status().discarded_stderr<512*1024 && std::chrono::steady_clock::now()<drain)std::this_thread::sleep_for(10ms);
            require(process.status().discarded_stderr>=512*1024,"Stderr must drain independently without blocking protocol output");
            process.shutdown();require(process.status().closed && process.status().exit_code==0,"Actual peer must exit gracefully after input EOF");
        }
        {
            McpStdioProcess process(config(argv,"descendant"));const auto result=discover(process);const auto pid=result["descendant"].get<DWORD>();
            HANDLE child=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);require(child!=nullptr,"Own fixture descendant must exist");
            const auto before=WaitForSingleObject(child,0);process.shutdown();const auto after=WaitForSingleObject(child,5000);CloseHandle(child);
            require(before==WAIT_TIMEOUT && after==WAIT_OBJECT_0,"Owned job must terminate its actual surviving descendant");
        }
        {
            McpStdioProcess process(config(argv,"silent"));
            fixture_ready(process);
            const auto frame=mcp_request("silent","server/discover","{}",McpWireEra::modern);process.write(frame,std::chrono::steady_clock::now()+5s);
            rejects<McpTransportTimeout>([&]{process.read(std::chrono::steady_clock::now()+150ms);});
            std::stop_source stop;stop.request_stop();rejects<McpTransportCancelled>([&]{process.read(std::chrono::steady_clock::now()+5s,stop.get_token());});
            require(!process.status().faulted,"Read timeout/cancellation alone must not invent peer failure or effect outcome");
        }
        const auto large=mcp_request("blocked","tools/call",nlohmann::json{{"payload",std::string(800000,'x')}}.dump(),McpWireEra::modern);
        {
            McpStdioProcess process(config(argv,"no-read"));
            fixture_ready(process);
            const auto start=std::chrono::steady_clock::now();rejects<McpTransportTimeout>([&]{process.write(large,start+150ms);});
            require(process.status().faulted && std::chrono::steady_clock::now()-start<5s,"Stalled pipe write must retire transport without an unbounded wait");
        }
        {
            McpStdioProcess process(config(argv,"no-read"));std::stop_source stop;
            fixture_ready(process);
            std::jthread cancel([&]{std::this_thread::sleep_for(150ms);stop.request_stop();});
            rejects<McpTransportCancelled>([&]{process.write(large,std::chrono::steady_clock::now()+5s,stop.get_token());});
            require(process.status().faulted,"Cancelled partial dispatch must prevent further frame writes");
        }
        {
            auto launch=config(argv,"flood");launch.stdout_buffer_limit=8192;McpStdioProcess process(launch);
            const auto deadline=std::chrono::steady_clock::now()+5s;while(!process.status().faulted && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(10ms);
            require(process.status().faulted && process.status().buffered_stdout<=8192,"Output flood must not exceed its actual bounded queue");
        }
        {
            McpStdioProcess process(config(argv,"exit"));require(!process.read(std::chrono::steady_clock::now()+5s),"Actual stdout EOF must be reported distinctly from timeout");
            const auto deadline=std::chrono::steady_clock::now()+5s;while(!process.status().exit_code && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(10ms);
            require(process.status().exit_code==7,"Actual child exit code must survive observation");
        }
        std::cout<<"Native MCP stdio transport passed actual independent Node pipe/process fixtures, environment isolation, quoting, stderr draining, EOF, stalled-write timeout/cancellation, output bounds and owned descendant cleanup. No model/tool effect or full MCP integration claimed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
