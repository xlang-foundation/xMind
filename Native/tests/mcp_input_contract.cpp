#include "agentflow/mcp_tool_registry.hpp"
#include "agentflow/persistence_service.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <random>
#include <thread>

using namespace agentflow;
using namespace std::chrono_literals;
namespace {
using Json=nlohmann::json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct FixtureClient final:McpToolClient{
    McpServerDescription description{McpWireEra::modern,"2026-07-28",R"({"tools":{}})","{}",""};
    bool ready()const override{return true;}
    const McpServerDescription& server()const override{return description;}
    void shutdown()noexcept override{}
    McpToolPage list_tools(std::optional<std::string>,Deadline,std::stop_token={})override{McpToolDescription tool;tool.name="fixture.ask";tool.description="Ask the user";tool.input_schema_json=R"({"type":"object","properties":{},"additionalProperties":false})";tool.annotations_json="{}";McpToolPage page;page.tools.push_back(std::move(tool));return page;}
private:
    McpToolReply call_tool(const std::string& name,std::string_view,Deadline deadline,std::stop_token cancel,const McpInputFulfillment& fulfill)override{
        require(name=="fixture.ask","Registry must retain the exact discovered peer tool");
        const auto answer=fulfill("opaque-peer-key",R"({"jsonrpc":"2.0","id":"peer-input","method":"elicitation/create","params":{"mode":"form","message":"Choose the target","requestedSchema":{"type":"object","properties":{"target":{"type":"string","minLength":1}},"required":["target"],"additionalProperties":false}}})");
        require(answer.has_value(),"The peer continuation must receive the submitted form response");
        const auto response=Json::parse(*answer);require(response.at("action")=="accept"&&response.at("content").at("target")=="TestProj","The MCP client must receive the validated user answer");
        return {"peer-final",R"({"jsonrpc":"2.0","id":"peer-final","result":{"content":[{"type":"text","text":"completed"}]}})",R"({"content":[{"type":"text","text":"completed"}]})",false,{}};
    }
};
std::int64_t expiry(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+30000;}
}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    const auto root=std::filesystem::temp_directory_path()/std::filesystem::path("xmind-mcp-input-"+std::to_string(std::random_device{}()));
    try{
        require(std::filesystem::create_directory(root),"Temporary workspace must be new");const auto database=(root/"state.sqlite").string();const std::vector<std::string> imports{argv[1],argv[2]};
        {
            PersistenceService store(database,imports);store.create_session("input-session","MCP form contract").get();store.start_prompt_run("input-run","input-session",R"({"content":"request peer form"})").get();store.transition("input-run",RunState::queued,RunState::running).get();
            WorkspaceTools workspace(root.string());FixtureClient peer;McpToolRegistry registry(peer,store,workspace,"fixture-server",1,std::chrono::steady_clock::now()+10s);const auto tools=registry.definitions();require(tools.size()==1,"Fixture MCP tool must be discovered");const auto alias=tools.front().name;
            std::string outcome;std::exception_ptr failure;std::thread worker([&]{try{outcome=registry.invoke("input-operation","input-run",alias,"{}",expiry(),std::chrono::steady_clock::now()+10s);}catch(...){failure=std::current_exception();}});
            Operation proposal;for(int i=0;i<500;++i){try{proposal=store.operation("input-operation").get();break;}catch(const NotFound&){std::this_thread::sleep_for(std::chrono::milliseconds(5));}}
            require(proposal.state==OperationState::awaiting_approval,"MCP operation must retain its normal explicit approval");store.decide_operation(proposal.id,OperationDecision::allow,"fixture-controller").get();
            std::vector<McpInputRequest> pending;for(int i=0;i<500&&pending.empty();++i){pending=store.mcp_input_requests("input-run").get();if(pending.empty())std::this_thread::sleep_for(std::chrono::milliseconds(5));}
            require(pending.size()==1&&pending.front().peer_tool=="fixture.ask"&&pending.front().request_key=="opaque-peer-key","Peer elicitation must be bound to the actual operation and request identity");
            const auto accepted=store.answer_mcp_input_request("input-run",pending.front().id,R"({"action":"accept","content":{"target":"TestProj"}})","local-owner").get();require(accepted.state=="answered","Local browser/editor answer must settle the pending request");worker.join();if(failure)std::rethrow_exception(failure);
            require(store.operation("input-operation").get().state==OperationState::succeeded&&Json::parse(outcome).at("acknowledged_by_peer")==true,"The accepted continuation must finish from the peer's actual tool reply");
            const auto events=store.events("input-run").get();require(std::count_if(events.begin(),events.end(),[](const auto& event){return event.kind=="mcp.input.requested";})==1&&std::count_if(events.begin(),events.end(),[](const auto& event){return event.kind=="mcp.input.answered";})==1,"MCP input request and answer must remain in the run event history");store.close();
        }
        std::filesystem::remove_all(root);std::cout<<"Native MCP elicitation contract passed: approved operation, durable scoped form, validated local answer, same-peer continuation and recorded outcome.\n";return 0;
    }catch(const std::exception& error){std::error_code ignored;std::filesystem::remove_all(root,ignored);std::cerr<<error.what()<<'\n';return 1;}
}
