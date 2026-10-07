// In-memory byte-stream fixtures verify the production wire codec only.
// No MCP subprocess, provider, remote tool effect or full interoperability claim.
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
#include <vector>
using namespace agentflow;
namespace {
void require(bool value,const char* error) {if(!value) throw std::runtime_error(error);}
template<class Action> void rejected(Action action) {try {action();} catch(const McpProtocolError&) {return;}throw std::runtime_error("Malformed MCP message was accepted");}
void invalid(const std::string& value) {
    std::size_t delivered=0;McpLineStream stream([&](const auto&){++delivered;});
    rejected([&]{stream.feed(value+"\n");});require(delivered==0,"Invalid message must not reach the consumer");
    rejected([&]{stream.feed(R"({"jsonrpc":"2.0","id":"later","result":{}})" "\n");});
}
}
int main() {
    try {
        using Json=nlohmann::json;
        std::vector<McpWireMessage> messages;McpLineStream stream([&](const auto& value){messages.push_back(value);});
        const std::string reply=R"({"jsonrpc":"2.0","id":"modern-1","result":{"resultType":"complete","content":[{"type":"text","text":"你好 🌍\nactual text"}]}})";
        const auto framed=reply+"\r\n";
        for(char byte:framed) stream.feed(std::string_view(&byte,1));
        require(messages.size()==1 && messages[0].kind==McpMessageKind::result && messages[0].id_json=="\"modern-1\"","Fragmented result must preserve request identity");
        require(messages[0].raw_json==reply,"Original message bytes must be retained");
        require(Json::parse(messages[0].payload_json)["content"][0]["text"]=="你好 🌍\nactual text","Fragmented UTF-8 and escaped newline must survive decoding");
        stream.feed(R"({"jsonrpc":"2.0","method":"notifications/progress","params":{"progress":1}})" "\n"
            R"({"jsonrpc":"2.0","id":18446744073709551615,"error":{"code":-32601,"message":"No method"}})" "\n"
            R"({"jsonrpc":"2.0","id":-9,"method":"sampling/createMessage","params":{}})" "\n"
            R"({"jsonrpc":"2.0","error":{"code":-32700,"message":"Malformed request"}})" "\n");
        require(messages.size()==5 && messages[1].kind==McpMessageKind::notification && messages[1].id_json.empty(),"Notifications must have no correlation ID");
        require(messages[2].kind==McpMessageKind::error && messages[2].id_json=="18446744073709551615","Integer IDs cannot lose precision through a double");
        require(messages[3].kind==McpMessageKind::request && messages[3].id_json=="-9","Legacy incoming requests remain distinguishable from responses");
        require(messages[4].kind==McpMessageKind::error && messages[4].id_json.empty(),"Malformed-request diagnostics cannot be mistaken for results");
        stream.finish();rejected([&]{stream.feed("\n");});

        const auto modern=mcp_request("probe-1","server/discover","{}",McpWireEra::modern);
        require(modern.back()=='\n' && modern.find('\n')==modern.size()-1,"Outgoing frames contain only one delimiter");
        const auto probe=Json::parse(modern);const auto& meta=probe["params"]["_meta"];
        require(meta["io.modelcontextprotocol/protocolVersion"]=="2026-07-28" && meta["io.modelcontextprotocol/clientCapabilities"].is_object(),"Every modern request needs pinned version and capabilities");
        const auto next=Json::parse(mcp_request("list-1","tools/list","{}",McpWireEra::modern));
        require(next["params"]["_meta"]==meta,"Metadata must be present again, not inferred from initialization");
        const auto legacy=Json::parse(mcp_request("init-1","initialize",R"({"protocolVersion":"2025-11-25","capabilities":{}})",McpWireEra::legacy));
        require(!legacy["params"].contains("_meta"),"Legacy wire encoding must not silently select modern metadata");
        const auto cancellation=Json::parse(mcp_notification("notifications/cancelled",R"({"requestId":"call-1"})"));
        require(!cancellation.contains("id") && cancellation["params"]["requestId"]=="call-1","Cancellation references a request but is not itself a request");
        rejected([&]{mcp_request("bad","tools/list",R"({"_meta":{"io.modelcontextprotocol/protocolVersion":"spoofed"}})",McpWireEra::modern);});
        rejected([&]{mcp_request("bad","tools/list","[]",McpWireEra::modern);});
        rejected([&]{mcp_request("bad","tools/list","{}",McpWireEra::modern,{"xMind","0.1.0","[]"});});

        for(const std::string malformed:{"", "stdout logging", "[]", "{\"jsonrpc\":\"1.0\",\"result\":{}}",
            R"({"jsonrpc":"2.0","id":null,"result":{}})",R"({"jsonrpc":"2.0","id":1.5,"result":{}})",
            R"({"jsonrpc":"2.0","id":"x","result":{},"error":{"code":1,"message":"bad"}})",
            R"({"jsonrpc":"2.0","id":"x","result":{},"id":"y"})",R"({"jsonrpc":"2.0","id":"x","result":{"nested":{"x":1,"x":2}}})",
            R"({"jsonrpc":"2.0","method":"tools/list","params":[]})",R"({"jsonrpc":"2.0","id":"x","result":[]})",
            R"({"jsonrpc":"2.0","id":"x","error":{"code":1.5,"message":"bad"}})",R"({"jsonrpc":"2.0","id":"x","result":{},"params":{}})"}) invalid(malformed);
        invalid(std::string(R"({"jsonrpc":"2.0","id":"x","result":{"text":")")+char(0xff)+R"("}})");
        invalid(std::string(R"({"jsonrpc":"2.0","id":"x","result":{"nested":)")+std::string(10000,'[')+"0"+std::string(10000,']')+"}}");
        invalid(std::string(1024*1024+1,'x'));
        McpLineStream incomplete([](const auto&){});incomplete.feed(R"({"jsonrpc":"2.0","id":"x","result":{}})");rejected([&]{incomplete.finish();});
        McpLineStream sinkFailure([](const auto&){throw std::runtime_error("fixture consumer failure");});
        try {sinkFailure.feed(reply+"\n");throw std::runtime_error("Consumer failure was hidden");}catch(const std::runtime_error& error){require(std::string(error.what())=="fixture consumer failure","Consumer exception must propagate");}
        rejected([&]{sinkFailure.feed(reply+"\n");});
        std::cout<<"Native MCP wire codec passed bounded fragmented JSON-RPC fixtures; no subprocess, remote tool or complete MCP support claimed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
