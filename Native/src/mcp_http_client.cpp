#include "agentflow/mcp_http_client.hpp"
#include "agentflow/mcp_http_transport.hpp"
#include "agentflow/mcp_http_stream.hpp"
#include "agentflow/mcp_tool_codec.hpp"
#include "agentflow/http_stream_transport.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <map>
#include <utility>

namespace agentflow {
namespace {
using Json=nlohmann::json;
void session(const std::string& value) {
    if(value.empty() || value.size()>4096)throw McpProtocolError("Invalid MCP HTTP session identity");
    for(unsigned char c:value)if(c<33 || c>126)throw McpProtocolError("Invalid MCP HTTP session identity");
}
std::string binding(const McpToolDescription& tool) {
    return Json::array({tool.name,tool.description,tool.input_schema_json,tool.annotations_json,tool.output_schema_json.value_or("")}).dump();
}
}
struct McpHttpClient::Impl {
    std::string endpoint,legacy_protocol="2025-11-25";
    std::optional<SecretBytes> bearer;
    std::optional<std::string> legacy_session;
    McpRequestTracker requests;
    McpHandshake handshake;
    bool started=false,live=false,closed=false;
    std::size_t rejected=0;
    struct Tool {std::string schema,binding;};
    std::map<std::string,Tool> tools;
    Impl(std::string url,std::optional<SecretBytes> secret):endpoint(std::move(url)),bearer(std::move(secret)),handshake(requests) {
        validate_mcp_http_endpoint(endpoint);
    }
    void retire() noexcept {
        live=false;closed=true;tools.clear();legacy_session.reset();bearer.reset();
        try{requests.abandon_all();}catch(...){}
    }
    std::optional<McpCorrelatedReply> exchange(const std::string& frame,McpWireEra era,
        const std::string& expected,Deadline deadline,std::stop_token cancel,bool& attempted,
        const std::optional<std::string>& schema={},bool initializing=false,std::string* observed_response=nullptr) {
        const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now());
        if(remaining.count()<=0)throw McpTransportTimeout("MCP HTTP request deadline exceeded");
        McpHttpPost input;input.url=endpoint;input.body=frame;input.era=era;input.legacy_protocol=legacy_protocol;
        input.input_schema=schema;input.deadline=std::min(remaining,std::chrono::milliseconds(600000));input.idle_timeout=input.deadline;
        if(era==McpWireEra::legacy)input.legacy_session=legacy_session;
        std::optional<McpCorrelatedReply> result;std::unique_ptr<McpHttpMessageStream> decoder;
        try {
            post_mcp_http(input,bearer?&*bearer:nullptr,[&](const auto& head){
                if(expected.empty()) {
                    if(head.status!=202)throw McpProtocolError("MCP notification did not receive an empty acknowledgement");
                    return;
                }
                if(head.status!=200 && head.status!=400 && head.status!=404)throw McpTransportError("MCP HTTP peer rejected the request (status "+std::to_string(head.status)+")");
                if(era==McpWireEra::legacy && head.legacy_session) {
                    session(*head.legacy_session);
                    if(initializing)legacy_session=head.legacy_session;
                    else if(!legacy_session || head.legacy_session!=legacy_session)throw McpProtocolError("MCP HTTP session identity changed");
                }
                decoder=std::make_unique<McpHttpMessageStream>(head.media_type,[&](const auto& message){
                    if(message.kind==McpMessageKind::notification)return;
                    if(message.kind==McpMessageKind::request)throw McpProtocolError("MCP HTTP server-request reply owner is not implemented");
                    const auto reply=requests.receive(message);
                    if(!reply || reply->request.id!=expected || result)throw McpProtocolError("MCP HTTP response identity changed");
                    if(observed_response)*observed_response=reply->response.raw_json;
                    result=*reply;
                });
            },[&](std::string_view bytes){if(!decoder)throw McpProtocolError("MCP notification returned response bytes");decoder->feed(bytes);},[&]{attempted=true;},cancel);
            if(decoder)decoder->finish();
            if(!expected.empty() && !result)throw McpProtocolError("MCP HTTP response ended without its correlated result");
            return result;
        }catch(const TransportCancelled&){throw McpTransportCancelled("MCP HTTP request cancelled");}
        catch(const TransportTimeout&){throw McpTransportTimeout("MCP HTTP request deadline exceeded");}
        catch(const TransportError&){throw McpTransportError("Native MCP HTTP transport failed");}
    }
};
McpHttpClient::McpHttpClient(std::string endpoint,std::optional<SecretBytes> bearer):impl_(std::make_unique<Impl>(std::move(endpoint),std::move(bearer))) {}
McpHttpClient::~McpHttpClient(){shutdown();}
void McpHttpClient::connect(Deadline deadline,std::stop_token cancel) {
    auto& state=*impl_;if(state.started || state.closed)throw McpProtocolError("MCP HTTP owner cannot reconnect or replay negotiation");state.started=true;
    try {
        auto request=state.handshake.begin();auto era=McpWireEra::modern;
        while(!state.handshake.ready()) {
            bool attempted=false;const auto reply=state.exchange(request.frame,era,request.request.id,deadline,cancel,attempted,{},request.request.method=="initialize");
            const auto action=state.handshake.accept(*reply);
            if(action.request){request=*action.request;era=McpWireEra::legacy;continue;}
            if(!action.notification.empty()) {
                state.legacy_protocol=Json::parse(reply->response.payload_json)["protocolVersion"].get<std::string>();
                if(state.legacy_protocol!="2025-11-25" && state.legacy_protocol!="2025-06-18")throw McpProtocolError("Deprecated MCP HTTP+SSE transport is not implemented");
                state.exchange(action.notification,McpWireEra::legacy,"",deadline,cancel,attempted);state.handshake.initialized_sent();
            }
        }
        state.live=true;
    }catch(...){state.retire();throw;}
}
McpToolPage McpHttpClient::list_tools(std::optional<std::string> cursor,Deadline deadline,std::stop_token cancel) {
    auto& state=*impl_;if(!ready())throw McpProtocolError("MCP HTTP client is not ready");
    if(cursor && (cursor->empty() || cursor->size()>4096 || cursor->find('\0')!=std::string::npos))throw std::invalid_argument("Invalid MCP tool cursor");
    if(!Json::parse(state.handshake.server().capabilities_json).contains("tools"))throw McpProtocolError("MCP server did not advertise tools");
    try {
        Json params=Json::object();if(cursor)params["cursor"]=*cursor;
        const auto request=state.requests.prepare("tools/list",params.dump(),state.handshake.server().era);bool attempted=false;
        auto page=mcp_decode_tool_page(state.exchange(request.frame,state.handshake.server().era,request.request.id,deadline,cancel,attempted)->response);
        std::vector<McpToolDescription> accepted;
        for(auto& tool:page.tools) {
            const auto found=state.tools.find(tool.name);
            try{McpHttpToolHeaders declarations(tool.input_schema_json);}
            catch(const McpProtocolError&){if(found!=state.tools.end())throw McpProtocolError("MCP HTTP catalogue changed after discovery");++state.rejected;continue;}
            const auto identity=binding(tool);
            if(found!=state.tools.end() && found->second.binding!=identity)throw McpProtocolError("MCP HTTP catalogue changed after discovery");
            if(found==state.tools.end()){if(state.tools.size()>=512)throw McpProtocolError("MCP HTTP catalogue exceeds owner limits");state.tools.emplace(tool.name,Impl::Tool{tool.input_schema_json,identity});}
            accepted.push_back(std::move(tool));
        }
        page.tools=std::move(accepted);return page;
    }catch(...){state.retire();throw;}
}
McpToolReply McpHttpClient::call_tool(const std::string& name,std::string_view arguments,Deadline deadline,std::stop_token cancel) {
    auto& state=*impl_;bool attempted=false;std::string request_id,response;
    try {
        if(!ready() || cancel.stop_requested() || std::chrono::steady_clock::now()>=deadline)throw McpTransportError("MCP HTTP tool stopped before dispatch");
        const auto found=state.tools.find(name);if(found==state.tools.end() || arguments.size()>65536)throw McpProtocolError("MCP HTTP tool was not discovered");
        // Validate/project before request admission and before the send boundary.
        (void)McpHttpToolHeaders(found->second.schema).project(arguments);
        const auto params="{\"name\":"+Json(name).dump()+",\"arguments\":"+mcp_compact_object(arguments)+"}";
        const auto request=state.requests.prepare("tools/call",params,state.handshake.server().era);request_id=request.request.id;
        const auto reply=state.exchange(request.frame,state.handshake.server().era,request_id,deadline,cancel,attempted,found->second.schema,false,&response);
        response=reply->response.raw_json;return mcp_decode_tool_reply(request_id,reply->response);
    }catch(...){state.retire();throw McpDispatchFailure(attempted,std::move(request_id),std::move(response));}
}
bool McpHttpClient::ready() const {return impl_->live && !impl_->closed;}
const McpServerDescription& McpHttpClient::server() const {if(!ready())throw McpProtocolError("MCP HTTP client is not ready");return impl_->handshake.server();}
std::size_t McpHttpClient::rejected_tools() const {return impl_->rejected;}
void McpHttpClient::shutdown() noexcept {if(impl_ && !impl_->closed)impl_->retire();}
}
