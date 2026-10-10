// In-memory byte-stream fixtures verify the production wire codec only.
// No MCP subprocess, provider, remote tool effect or full interoperability claim.
#include "agentflow/mcp_wire.hpp"
#include "agentflow/mcp_requests.hpp"
#include "agentflow/mcp_http_metadata.hpp"
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
        const auto unsupported=Json::parse(mcp_peer_reply(messages[3],McpWireEra::legacy));
        require(unsupported["id"]==-9 && unsupported["error"]["code"]==-32601 && !unsupported.contains("result"),"Unsupported peer methods must receive a correlated error without executing work");
        rejected([&]{mcp_peer_reply(messages[3],McpWireEra::modern);});
        auto forged=messages[3];forged.id_json="123";rejected([&]{mcp_peer_reply(forged,McpWireEra::legacy);});
        rejected([&]{mcp_peer_reply(messages[1],McpWireEra::legacy);});
        for(const auto& id:{Json("ping\"雪"),Json(std::uint64_t(18446744073709551615ULL)),Json(-9)}) {
            McpLineStream peer([&](const auto& request){const auto answer=Json::parse(mcp_peer_reply(request,McpWireEra::legacy));require(answer["id"]==id && answer["result"]==Json::object() && !answer.contains("error"),"Legacy ping must preserve exact integer/string identity and return an empty result");});
            peer.feed(Json{{"jsonrpc","2.0"},{"id",id},{"method","ping"}}.dump()+"\n");
        }
        require(messages[4].kind==McpMessageKind::error && messages[4].id_json.empty(),"Malformed-request diagnostics cannot be mistaken for results");
        stream.finish();rejected([&]{stream.feed("\n");});

        const auto modern=mcp_request("probe-1","server/discover","{}",McpWireEra::modern);
        require(modern.back()=='\n' && modern.find('\n')==modern.size()-1,"Outgoing frames contain only one delimiter");
        const auto probe=Json::parse(modern);const auto& meta=probe["params"]["_meta"];
        require(meta["io.modelcontextprotocol/protocolVersion"]=="2026-07-28" && meta["io.modelcontextprotocol/clientCapabilities"].is_object(),"Every modern request needs pinned version and capabilities");
        const auto next=Json::parse(mcp_request("list-1","tools/list","{}",McpWireEra::modern));
        require(next["params"]["_meta"]==meta,"Metadata must be present again, not inferred from initialization");
        const std::string precise="{\n \"arguments\": { \"decimal\": 1.00000000000000000001, \"text\": \"keep  space\\n\\\"quote\" } }";
        for(const auto era:{McpWireEra::legacy,McpWireEra::modern}) {
            const auto actual=mcp_request("precise","tools/call",precise,era);
            require(actual.find("1.00000000000000000001")!=std::string::npos && actual.find("keep  space\\n\\\"quote")!=std::string::npos,"Request encoding must preserve exact numbers and escaped string bytes");
            require(actual.find('\n')==actual.size()-1,"Argument formatting cannot inject a second stdio frame");
        }
        require(mcp_compact_object(" { \"n\": -0.0000e+12 } ")=="{\"n\":-0.0000e+12}","Lexical compaction cannot normalize numeric tokens");
        const auto precise_payload=R"({"jsonrpc":"2.0","id":"precise","result":{"value":1.00000000000000000001,"schema":{"const":2.00000000000000000001}}})";
        McpLineStream precise_stream([&](const auto& value){require(value.payload_json.find("1.00000000000000000001")!=std::string::npos,"Decoded result must preserve exact number tokens");const auto schema=mcp_object_member(value.payload_json,"schema");require(schema && schema->find("2.00000000000000000001")!=std::string::npos,"Extracted nested schema must retain actual source tokens");});precise_stream.feed(std::string(precise_payload)+"\n");
        const auto raw=mcp_array_values(R"([ {"x":[1,2],"quote":"a,}\\\""}, 1.00000000000000000001 ])");
        require(raw.size()==2 && raw[1]=="1.00000000000000000001" && mcp_object_member(raw[0],"x")=="[1,2]","Raw extraction must respect quoted delimiters and nested arrays");
        require(!mcp_object_member("{}","absent") && mcp_array_values("[]").empty(),"Empty members and collections cannot fabricate values");
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
        McpRequestTracker tracker(2);
        const auto first=tracker.prepare("server/discover","{}",McpWireEra::modern);
        const auto second=tracker.prepare("tools/list","{}",McpWireEra::modern);
        require(first.request.id!=second.request.id && tracker.pending()==2,"Requests must have unique bounded pending identities");
        rejected([&]{tracker.prepare("tools/list","{}",McpWireEra::modern);});
        auto response=[&](const std::string& id,bool failure=false) {
            std::optional<McpWireMessage> decoded;McpLineStream input([&](const auto& value){decoded=value;});
            Json value{{"jsonrpc","2.0"},{"id",id}};
            if(failure) value["error"]={{"code",-32601},{"message","Fixture peer error"}};else value["result"]={{"resultType","complete"}};
            input.feed(value.dump()+"\n");return *decoded;
        };
        const auto secondReply=tracker.receive(response(second.request.id));
        require(secondReply && secondReply->request.method=="tools/list" && tracker.pending()==1,"Out-of-order replies must correlate by exact ID");
        rejected([&]{tracker.receive(response(second.request.id));});
        const auto peerError=tracker.receive(response(first.request.id,true));
        require(peerError && peerError->response.kind==McpMessageKind::error && tracker.pending()==0,"Peer RPC errors must correlate without being turned into success");
        rejected([&]{tracker.prepare("bad","[]",McpWireEra::modern);});require(tracker.pending()==0,"Invalid arguments cannot consume admission capacity");
        const auto cancelled=tracker.prepare("tools/call",R"({"name":"fixture","arguments":{}})",McpWireEra::modern);
        const auto cancel=Json::parse(tracker.cancel(cancelled.request.id));
        require(cancel["params"]["requestId"]==cancelled.request.id && tracker.pending()==0,"Cancellation must retire exactly its request");
        require(!tracker.receive(response(cancelled.request.id)),"Late cancelled replies must not reach the result consumer");
        rejected([&]{tracker.cancel(cancelled.request.id);});
        const auto initialization=tracker.prepare("initialize","{}",McpWireEra::legacy);
        rejected([&]{tracker.cancel(initialization.request.id);});require(tracker.pending()==1,"Initialization cannot be cancelled on the wire");
        const auto effect=tracker.prepare("tools/call",R"({"name":"fixture","arguments":{}})",McpWireEra::legacy);
        const auto lost=tracker.abandon_all();require(lost.size()==2 && tracker.pending()==0,"Transport loss must preserve the identities requiring owner recovery");
        require(!tracker.receive(response(effect.request.id)),"Abandoned replies must not complete a new request");
        const auto fresh=tracker.prepare("tools/list","{}",McpWireEra::legacy);require(fresh.request.id!=effect.request.id,"Abandonment must never recycle an ID");
        rejected([&]{tracker.receive(response("not-issued"));});require(tracker.pending()==1,"Unknown IDs cannot consume another request");
        for(int i=0;i<300;++i) {const auto item=tracker.prepare("tools/list","{}",McpWireEra::legacy);tracker.cancel(item.request.id);}
        rejected([&]{tracker.receive(response(cancelled.request.id));});
        require(tracker.pending()==1,"Retirement history is bounded without evicting active requests");
        const auto http=mcp_http_request_headers(mcp_request("http","tools/call",R"({"name":"雪","arguments":{}})",McpWireEra::modern),McpWireEra::modern);
        require(http.size()==3 && http[0].value=="2026-07-28" && http[1].value=="tools/call" && http[2].value=="=?base64?6Zuq?=","HTTP headers must derive from actual modern request bytes and encode Unicode names");
        const auto oldHttp=mcp_http_request_headers(mcp_request("http","tools/list","{}",McpWireEra::legacy),McpWireEra::legacy);
        require(oldHttp.size()==1 && oldHttp[0].value=="2025-11-25","Older HTTP requests cannot invent modern mirror metadata");
        rejected([&]{mcp_http_request_headers(mcp_request("http","tools/list","{}",McpWireEra::legacy),McpWireEra::modern);});
        rejected([&]{mcp_http_request_headers(mcp_request("http","tools/call","{}",McpWireEra::modern),McpWireEra::modern);});
        rejected([&]{mcp_http_request_headers(mcp_request("http","bad\r\nInjected","{}",McpWireEra::modern),McpWireEra::modern);});
        McpHttpToolHeaders projection(R"({"type":"object","properties":{"n":{"type":"integer","x-mcp-header":"Count"},"context":{"type":"object","properties":{"region":{"type":"string","x-mcp-header":"Region"}}},"flag":{"type":"boolean","x-mcp-header":"Enabled"}}})");
        const auto projected=projection.project(R"({"n":9007199254740991,"context":{"region":"line1\nline2"},"flag":false})");
        require(projected.size()==3 && projected[0].name=="Mcp-Param-Region" && projected[0].value=="=?base64?bGluZTEKbGluZTI=?=" && projected[1].value=="false" && projected[2].value=="9007199254740991","Nested paths, control encoding, false and maximum exact integer must survive HTTP projection");
        require(projection.project(R"({"context":null,"n":null})").empty(),"Absent/null mirrored values must omit headers");
        require(projection.project(R"({"n":-9007199254740991})")[0].value=="-9007199254740991","Minimum safe integer must remain exact");
        require(projection.project(R"({"n":420e-1})")[0].value=="42","Exact exponent integer must use decimal header spelling");
        require(projection.project(R"({"n":42.000})")[0].value=="42","Exact decimal integer must use decimal header spelling");
        require(projection.project(R"({"n":-0.000})")[0].value=="0","Negative zero must have canonical integer spelling");
        for(const auto& raw:{R"({"n":9007199254740992})",R"({"n":-9007199254740992})",R"({"n":1.00000000000000000001})",R"({"n":1e-20})",R"({"n":"42"})",R"({"flag":0})",R"({"context":[]})"})rejected([&]{projection.project(raw);});
        McpHttpToolHeaders literal(R"({"properties":{"text":{"type":"string","x-mcp-header":"Text"}}})");
        require(literal.project(R"({"text":"=?base64?literal?="})")[0].value=="=?base64?PT9iYXNlNjQ/bGl0ZXJhbD89?=","Sentinel-looking literals must be encoded to avoid ambiguity");
        require(literal.project(R"({"text":" padded "})")[0].value=="=?base64?IHBhZGRlZCA=?=","Padded strings must preserve whitespace through Base64");
        require(literal.project(R"({"text":"a\tb"})")[0].value=="a\tb","Interior horizontal tabs are valid HTTP field values");
        require(literal.project(R"({"text":""})")[0].value.empty(),"Present empty strings must emit empty headers");
        for(const auto& schema:{R"({"properties":{"x":{"type":"number","x-mcp-header":"X"}}})",R"({"properties":{"x":{"type":"string","x-mcp-header":""}}})",R"({"properties":{"x":{"type":"string","x-mcp-header":"Bad\r\nName"}}})",R"({"properties":{"x":{"type":"string","x-mcp-header":"X"},"y":{"type":"string","x-mcp-header":"x"}}})",R"({"items":{"properties":{"x":{"type":"string","x-mcp-header":"X"}}}})",R"({"allOf":[{"properties":{"x":{"type":"string","x-mcp-header":"X"}}}]})",R"({"type":"string","x-mcp-header":"Root"})",R"({"$defs":{"hidden":{"properties":{"x":{"type":"string","x-mcp-header":"X"}}}}})"})rejected([&]{McpHttpToolHeaders invalidHeaders(schema);});
        rejected([&]{literal.project(Json{{"text",std::string(16385,'x')}}.dump());});
        rejected([&]{literal.project(R"({"text":"x","text":"y"})");});
        std::cout<<"Native MCP wire codec and HTTP metadata projection passed bounded fixtures; no subprocess, HTTP network binding, remote tool or complete MCP support claimed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
