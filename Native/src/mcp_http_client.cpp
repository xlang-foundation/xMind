#include "agentflow/mcp_http_client.hpp"
#include "agentflow/mcp_http_transport.hpp"
#include "agentflow/mcp_http_stream.hpp"
#include "agentflow/mcp_tool_codec.hpp"
#include "agentflow/http_stream_transport.hpp"
#include "agentflow/mcp_oauth.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <map>
#include <set>
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
void cursor(const std::optional<std::string>& value){if(value&&(value->empty()||value->size()>4096||value->find('\0')!=std::string::npos))throw std::invalid_argument("Invalid MCP metadata cursor");}
void identity(const std::string& value,const char* label,std::size_t limit){if(value.empty()||value.size()>limit||value.find('\0')!=std::string::npos)throw std::invalid_argument(std::string("Invalid MCP ")+label);}
void capability(const McpServerDescription& server,const char* name){if(!Json::parse(server.capabilities_json).contains(name))throw McpProtocolError(std::string("MCP server did not advertise ")+name);}
}
struct McpHttpClient::Impl {
    std::string endpoint,legacy_protocol="2025-11-25";
    std::optional<SecretBytes> bearer;
    std::optional<McpBearerValidity> validity;
    std::optional<std::string> legacy_session;
    McpRequestTracker requests;
    McpHandshake handshake;
    bool started=false,live=false,closed=false;
    std::size_t rejected=0;
    struct Tool {std::string schema,binding;};
    std::map<std::string,Tool> tools;
    Impl(std::string url,std::optional<SecretBytes> secret,std::optional<McpBearerValidity> lifetime):endpoint(std::move(url)),bearer(std::move(secret)),validity(lifetime),handshake(requests) {
        validate_mcp_http_endpoint(endpoint);
        if(validity&&(!bearer||validity->acquired_unix_ms<=0||(validity->expires_unix_ms&&*validity->expires_unix_ms<=validity->acquired_unix_ms)))throw std::invalid_argument("Invalid native MCP bearer lifetime");
    }
    void retire() noexcept {
        live=false;closed=true;tools.clear();legacy_session.reset();bearer.reset();
        try{requests.abandon_all();}catch(...){}
    }
    std::optional<McpCorrelatedReply> exchange(const std::string& frame,McpWireEra era,
        const std::string& expected,Deadline deadline,std::stop_token cancel,bool& attempted,
        const std::optional<std::string>& schema={},bool initializing=false,std::string* observed_response=nullptr,
        const std::function<void(const McpWireMessage&)>& notification={},bool require_event_stream=false) {
        if(validity){const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();if(now<validity->acquired_unix_ms||(validity->expires_unix_ms&&now>=*validity->expires_unix_ms))throw McpOAuthAuthorizationRequired({});}
        const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now());
        if(remaining.count()<=0)throw McpTransportTimeout("MCP HTTP request deadline exceeded");
        McpHttpPost input;input.url=endpoint;input.body=frame;input.era=era;input.legacy_protocol=legacy_protocol;
        input.input_schema=schema;input.deadline=std::min(remaining,std::chrono::milliseconds(600000));input.idle_timeout=input.deadline;
        if(era==McpWireEra::legacy)input.legacy_session=legacy_session;
        std::optional<McpCorrelatedReply> result;std::unique_ptr<McpHttpMessageStream> decoder;
        try {
            post_mcp_http(input,bearer?&*bearer:nullptr,[&](const auto& head){
                if(head.status==401 || head.status==403)throw McpOAuthAuthorizationRequired(head.authenticate?mcp_oauth_bearer_challenge(*head.authenticate):std::nullopt);
                if(expected.empty()) {
                    if(head.status!=202)throw McpProtocolError("MCP notification did not receive an empty acknowledgement");
                    return;
                }
                if(head.status!=200 && head.status!=400 && head.status!=404)throw McpTransportError("MCP HTTP peer rejected the request (status "+std::to_string(head.status)+")");
                if(require_event_stream && head.media_type!="text/event-stream")throw McpProtocolError("MCP subscriptions/listen requires a request-scoped SSE response");
                if(era==McpWireEra::legacy && head.legacy_session) {
                    session(*head.legacy_session);
                    if(initializing)legacy_session=head.legacy_session;
                    else if(!legacy_session || head.legacy_session!=legacy_session)throw McpProtocolError("MCP HTTP session identity changed");
                }
                decoder=std::make_unique<McpHttpMessageStream>(head.media_type,[&](const auto& message){
                    if(message.kind==McpMessageKind::notification){if(notification)notification(message);return;}
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
McpHttpClient::McpHttpClient(std::string endpoint,std::optional<SecretBytes> bearer,std::optional<McpBearerValidity> validity):impl_(std::make_unique<Impl>(std::move(endpoint),std::move(bearer),validity)) {}
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
McpMetadataPage McpHttpClient::list_resources(std::optional<std::string> next,Deadline deadline,std::stop_token cancel){
    auto& state=*impl_;if(!ready())throw McpProtocolError("MCP HTTP client is not ready");cursor(next);capability(state.handshake.server(),"resources");
    try{Json params=Json::object();if(next)params["cursor"]=*next;const auto request=state.requests.prepare("resources/list",params.dump(),state.handshake.server().era);bool attempted=false;return mcp_decode_resource_page(state.exchange(request.frame,state.handshake.server().era,request.request.id,deadline,cancel,attempted)->response);}
    catch(...){state.retire();throw;}
}
McpReadOnlyReply McpHttpClient::read_resource(const std::string& uri,Deadline deadline,std::stop_token cancel){
    auto& state=*impl_;if(!ready())throw McpProtocolError("MCP HTTP client is not ready");identity(uri,"resource URI",8192);capability(state.handshake.server(),"resources");
    try{const auto request=state.requests.prepare("resources/read",Json{{"uri",uri}}.dump(),state.handshake.server().era);bool attempted=false;const auto reply=state.exchange(request.frame,state.handshake.server().era,request.request.id,deadline,cancel,attempted);if(!reply)throw McpProtocolError("MCP resource read ended without a correlated result");return mcp_decode_resource_reply(reply->request.id,reply->response);}
    catch(...){state.retire();throw;}
}
McpMetadataPage McpHttpClient::list_prompts(std::optional<std::string> next,Deadline deadline,std::stop_token cancel){
    auto& state=*impl_;if(!ready())throw McpProtocolError("MCP HTTP client is not ready");cursor(next);capability(state.handshake.server(),"prompts");
    try{Json params=Json::object();if(next)params["cursor"]=*next;const auto request=state.requests.prepare("prompts/list",params.dump(),state.handshake.server().era);bool attempted=false;return mcp_decode_prompt_page(state.exchange(request.frame,state.handshake.server().era,request.request.id,deadline,cancel,attempted)->response);}
    catch(...){state.retire();throw;}
}
McpReadOnlyReply McpHttpClient::get_prompt(const std::string& name,std::string_view arguments,Deadline deadline,std::stop_token cancel){
    auto& state=*impl_;if(!ready())throw McpProtocolError("MCP HTTP client is not ready");identity(name,"prompt name",128);capability(state.handshake.server(),"prompts");if(arguments.size()>65536)throw std::invalid_argument("MCP prompt arguments exceed limits");const auto compact=mcp_compact_object(arguments);
    try{const auto request=state.requests.prepare("prompts/get",Json{{"name",name},{"arguments",Json::parse(compact)}}.dump(),state.handshake.server().era);bool attempted=false;const auto reply=state.exchange(request.frame,state.handshake.server().era,request.request.id,deadline,cancel,attempted);if(!reply)throw McpProtocolError("MCP prompt retrieval ended without a correlated result");return mcp_decode_prompt_reply(reply->request.id,reply->response);}
    catch(...){state.retire();throw;}
}
McpSubscriptionAcknowledgement McpHttpClient::listen_subscriptions(const McpSubscriptionFilter& requested,
    const std::function<void(const McpWireMessage&)>& on_notification,Deadline deadline,std::stop_token cancel) {
    auto& state=*impl_;if(!ready())throw McpProtocolError("MCP HTTP client is not ready");
    if(state.handshake.server().era!=McpWireEra::modern)throw McpProtocolError("MCP subscriptions/listen requires protocol 2026-07-28");
    if(requested.resource_subscriptions.size()>128)throw McpProtocolError("MCP resource subscription count exceeds limits");
    auto uris=requested.resource_subscriptions;for(const auto& uri:uris)identity(uri,"resource subscription URI",8192);std::sort(uris.begin(),uris.end());
    if(std::adjacent_find(uris.begin(),uris.end())!=uris.end())throw McpProtocolError("Duplicate MCP resource subscription URI");
    if(!requested.tools_list_changed&&!requested.prompts_list_changed&&!requested.resources_list_changed&&uris.empty())throw McpProtocolError("MCP subscriptions/listen requires at least one notification filter");
    Json filters=Json::object();if(requested.tools_list_changed)filters["toolsListChanged"]=true;if(requested.prompts_list_changed)filters["promptsListChanged"]=true;if(requested.resources_list_changed)filters["resourcesListChanged"]=true;if(!uris.empty())filters["resourceSubscriptions"]=uris;
    try {
        const auto request=state.requests.prepare("subscriptions/listen",Json{{"notifications",filters}}.dump(),McpWireEra::modern);
        const Json request_id=request.request.id;
        std::optional<McpSubscriptionAcknowledgement> acknowledgement;
        const auto matches_id=[&](const Json& metadata){return metadata.is_object()&&metadata.contains("io.modelcontextprotocol/subscriptionId")&&metadata.at("io.modelcontextprotocol/subscriptionId")==request_id;};
        const auto validate_ack=[&](const McpWireMessage& message){
            if(message.method!="notifications/subscriptions/acknowledged")throw McpProtocolError("MCP subscription notification arrived before its acknowledgement");
            const auto params=Json::parse(message.payload_json);
            if(!params.is_object()||!params.contains("_meta")||!matches_id(params.at("_meta"))||!params.contains("notifications")||!params.at("notifications").is_object())throw McpProtocolError("Invalid MCP subscription acknowledgement");
            const auto& accepted=params.at("notifications");
            for(auto it=accepted.begin();it!=accepted.end();++it)if(it.key()!="toolsListChanged"&&it.key()!="promptsListChanged"&&it.key()!="resourcesListChanged"&&it.key()!="resourceSubscriptions")throw McpProtocolError("MCP subscription acknowledgement contains an unknown filter");
            McpSubscriptionFilter result;
            const auto flag=[&](const char* key,bool asked){if(!accepted.contains(key))return false;if(!accepted.at(key).is_boolean()||!accepted.at(key).get<bool>()||!asked)throw McpProtocolError("MCP server acknowledged an unrequested subscription filter");return true;};
            result.tools_list_changed=flag("toolsListChanged",requested.tools_list_changed);result.prompts_list_changed=flag("promptsListChanged",requested.prompts_list_changed);result.resources_list_changed=flag("resourcesListChanged",requested.resources_list_changed);
            if(accepted.contains("resourceSubscriptions")){const auto& values=accepted.at("resourceSubscriptions");if(!values.is_array()||values.size()>uris.size())throw McpProtocolError("Invalid acknowledged MCP resource subscription list");std::set<std::string> seen;for(const auto& value:values){if(!value.is_string())throw McpProtocolError("Invalid acknowledged MCP resource subscription URI");const auto uri=value.get<std::string>();if(!std::binary_search(uris.begin(),uris.end(),uri)||!seen.insert(uri).second)throw McpProtocolError("MCP server acknowledged an unrequested resource subscription");result.resource_subscriptions.push_back(uri);}}
            const auto capabilities=Json::parse(state.handshake.server().capabilities_json);
            const auto advertised=[&](const char* section,const char* key){return capabilities.contains(section)&&capabilities.at(section).is_object()&&capabilities.at(section).contains(key)&&capabilities.at(section).at(key).is_boolean()&&capabilities.at(section).at(key).get<bool>();};
            if(result.tools_list_changed&&!advertised("tools","listChanged"))throw McpProtocolError("MCP server acknowledged an unadvertised tools-list subscription");if(result.prompts_list_changed&&!advertised("prompts","listChanged"))throw McpProtocolError("MCP server acknowledged an unadvertised prompts-list subscription");if(result.resources_list_changed&&!advertised("resources","listChanged"))throw McpProtocolError("MCP server acknowledged an unadvertised resources-list subscription");if(!result.resource_subscriptions.empty()&&!advertised("resources","subscribe"))throw McpProtocolError("MCP server acknowledged unadvertised resource subscriptions");
            return McpSubscriptionAcknowledgement{request.request.id,std::move(result)};
        };
        bool attempted=false;const auto reply=state.exchange(request.frame,McpWireEra::modern,request.request.id,deadline,cancel,attempted,{},false,nullptr,[&](const McpWireMessage& message){
            if(!acknowledgement){acknowledgement=validate_ack(message);return;}
            if(message.method=="notifications/subscriptions/acknowledged")throw McpProtocolError("Duplicate MCP subscription acknowledgement");
            const auto params=Json::parse(message.payload_json);if(!params.is_object()||!params.contains("_meta")||!matches_id(params.at("_meta")))throw McpProtocolError("MCP subscription notification has the wrong subscription ID");
            if(message.method=="notifications/tools/list_changed"){if(!acknowledgement->notifications.tools_list_changed)throw McpProtocolError("MCP server sent an unrequested tools-list notification");}
            else if(message.method=="notifications/prompts/list_changed"){if(!acknowledgement->notifications.prompts_list_changed)throw McpProtocolError("MCP server sent an unrequested prompts-list notification");}
            else if(message.method=="notifications/resources/list_changed"){if(!acknowledgement->notifications.resources_list_changed)throw McpProtocolError("MCP server sent an unrequested resources-list notification");}
            else if(message.method=="notifications/resources/updated"){if(!params.contains("uri")||!params.at("uri").is_string()||std::find(acknowledgement->notifications.resource_subscriptions.begin(),acknowledgement->notifications.resource_subscriptions.end(),params.at("uri").get<std::string>())==acknowledgement->notifications.resource_subscriptions.end())throw McpProtocolError("MCP server sent an unrequested resource update");}
            else throw McpProtocolError("MCP server sent an unsupported subscription notification");
            if(on_notification)on_notification(message);
        },true);
        if(!acknowledgement)throw McpProtocolError("MCP subscription stream closed before acknowledgement");if(!reply)throw McpProtocolError("MCP subscription stream closed without a completion response");
        const auto result=Json::parse(reply->response.payload_json);if(!result.is_object()||!result.contains("resultType")||result.at("resultType")!="complete"||!result.contains("_meta")||!matches_id(result.at("_meta")))throw McpProtocolError("Invalid MCP graceful subscription completion");
        return std::move(*acknowledgement);
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
