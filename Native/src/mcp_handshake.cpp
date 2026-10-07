#include "agentflow/mcp_handshake.hpp"
#include "nlohmann/json.hpp"
#include <set>
#include <utility>

namespace agentflow {
namespace {
using Json=nlohmann::json;
Json object(const std::string& source) {
    if(source.size()>1024*1024)throw McpProtocolError("MCP negotiation payload exceeds limits");
    std::vector<std::set<std::string>> fields;
    try {
        auto value=Json::parse(source,[&](int depth,Json::parse_event_t event,Json& parsed){
            if(depth>64)throw McpProtocolError("MCP negotiation nesting exceeds limits");
            if(event==Json::parse_event_t::object_start)fields.emplace_back();else if(event==Json::parse_event_t::object_end)fields.pop_back();
            else if(event==Json::parse_event_t::key && !fields.back().insert(parsed.get<std::string>()).second)throw McpProtocolError("Duplicate MCP negotiation field");return true;
        });
        if(!value.is_object())throw McpProtocolError("MCP negotiation payload must be an object");return value;
    }catch(const Json::exception&){throw McpProtocolError("Invalid MCP negotiation JSON");}
}
std::string text(const Json& value,const char* key,std::size_t limit) {
    if(!value.contains(key) || !value[key].is_string())throw McpProtocolError("Missing MCP negotiation text field");
    auto result=value[key].get<std::string>();if(result.empty() || result.size()>limit || result.find('\0')!=std::string::npos)throw McpProtocolError("MCP negotiation text exceeds limits");return result;
}
Json capabilities(const Json& result) {
    if(!result.contains("capabilities") || !result["capabilities"].is_object())throw McpProtocolError("Missing MCP server capabilities");
    const auto& value=result["capabilities"];
    for(const auto* name:{"tools","resources","prompts","logging","completions","tasks","extensions","experimental"})if(value.contains(name) && !value[name].is_object())throw McpProtocolError("Invalid MCP server capability");
    return value;
}
Json identity(const Json& value) {
    if(!value.is_object())throw McpProtocolError("Invalid MCP server identity");text(value,"name",128);text(value,"version",64);return value;
}
void complete(const Json& value) {if(value.contains("resultType") && value["resultType"]!="complete")throw McpProtocolError("MCP discovery/initialization did not complete");}
std::string instructions(const Json& value) {
    if(!value.contains("instructions"))return {};
    if(!value["instructions"].is_string())throw McpProtocolError("Invalid MCP server instructions");
    auto result=value["instructions"].get<std::string>();if(result.size()>65536 || result.find('\0')!=std::string::npos)throw McpProtocolError("MCP instructions exceed limits");return result;
}
}
struct McpHandshake::Impl {
    enum class Phase {idle,probe,initialize,ack,modern_ready,legacy_ready,failed};
    McpRequestTracker& requests;McpClientIdentity client;Phase phase=Phase::idle;
    std::string expected_id,expected_method;
    McpServerDescription description{McpWireEra::modern,"","{}","{}",""};
    Impl(McpRequestTracker& tracker,McpClientIdentity info):requests(tracker),client(std::move(info)) {}
    McpPreparedRequest start(std::string method,const std::string& params,McpWireEra era,Phase next) {
        auto request=requests.prepare(method,params,era,client);expected_id=request.request.id;expected_method=std::move(method);phase=next;return request;
    }
    McpPreparedRequest legacy() {
        return start("initialize",Json{{"protocolVersion","2025-11-25"},{"capabilities",object(client.capabilities_json)},
            {"clientInfo",{{"name",client.name},{"version",client.version}}}}.dump(),McpWireEra::legacy,Phase::initialize);
    }
};
McpHandshake::McpHandshake(McpRequestTracker& requests,McpClientIdentity identity):impl_(std::make_unique<Impl>(requests,std::move(identity))) {}
McpHandshake::~McpHandshake()=default;
McpPreparedRequest McpHandshake::begin() {
    auto& state=*impl_;if(state.phase!=Impl::Phase::idle || state.requests.pending())throw McpProtocolError("MCP negotiation must start on an idle request tracker");
    try{return state.start("server/discover","{}",McpWireEra::modern,Impl::Phase::probe);}catch(...){state.phase=Impl::Phase::failed;throw;}
}
McpHandshakeAction McpHandshake::accept(const McpCorrelatedReply& reply) {
    auto& state=*impl_;
    try {
        if((state.phase!=Impl::Phase::probe && state.phase!=Impl::Phase::initialize) || reply.request.id!=state.expected_id || reply.request.method!=state.expected_method || reply.response.id_json!=Json(state.expected_id).dump())throw McpProtocolError("MCP negotiation response does not match its phase/request");
        const auto value=object(reply.response.payload_json);
        if(reply.response.kind==McpMessageKind::error) {
            if(!value.contains("code") || !value["code"].is_number_integer())throw McpProtocolError("Invalid MCP negotiation error");
            const bool modern=!value["code"].is_number_unsigned() && (value["code"]==-32020 || value["code"]==-32021 || value["code"]==-32022);
            if(state.phase==Impl::Phase::probe && !modern)return {state.legacy(),{}};
            // There is only one supported modern revision. A rejection cannot
            // produce a mutually supported alternative or justify legacy fallback.
            throw McpProtocolError(modern?"Modern MCP server rejected the supported discovery version/capabilities":"MCP legacy initialization was rejected");
        }
        if(reply.response.kind!=McpMessageKind::result)throw McpProtocolError("MCP negotiation requires a result or error");
        complete(value);const auto declared=capabilities(value);
        if(state.phase==Impl::Phase::probe) {
            if(!value.contains("supportedVersions") || !value["supportedVersions"].is_array() || value["supportedVersions"].empty() || value["supportedVersions"].size()>16)throw McpProtocolError("Invalid MCP discovery versions");
            std::set<std::string> versions;
            for(const auto& version:value["supportedVersions"]){if(!version.is_string())throw McpProtocolError("Invalid MCP discovery version");const auto name=version.get<std::string>();if(name.size()!=10 || !versions.insert(name).second)throw McpProtocolError("Invalid/duplicate MCP discovery version");for(std::size_t i=0;i<name.size();++i)if((i==4 || i==7)?name[i]!='-':name[i]<'0' || name[i]>'9')throw McpProtocolError("Invalid MCP discovery version format");}
            if(!versions.contains("2026-07-28"))throw McpProtocolError("No mutually supported modern MCP version");
            Json server=Json::object();
            if(value.contains("_meta")) {if(!value["_meta"].is_object())throw McpProtocolError("Invalid MCP discovery metadata");if(value["_meta"].contains("io.modelcontextprotocol/serverInfo"))server=identity(value["_meta"]["io.modelcontextprotocol/serverInfo"]);}
            state.description={McpWireEra::modern,"2026-07-28",declared.dump(),server.dump(),instructions(value)};state.phase=Impl::Phase::modern_ready;return {};
        }
        const auto version=text(value,"protocolVersion",10);
        if(version!="2025-11-25" && version!="2025-06-18" && version!="2024-11-05")throw McpProtocolError("Unsupported MCP legacy initialization version");
        if(!value.contains("serverInfo"))throw McpProtocolError("Missing MCP legacy server identity");
        state.description={McpWireEra::legacy,version,declared.dump(),identity(value["serverInfo"]).dump(),instructions(value)};
        auto notification=mcp_notification("notifications/initialized");state.phase=Impl::Phase::ack;return {{},std::move(notification)};
    }catch(...){state.phase=Impl::Phase::failed;throw;}
}
McpPreparedRequest McpHandshake::probe_timeout() {
    auto& state=*impl_;if(state.phase!=Impl::Phase::probe)throw McpProtocolError("Only MCP discovery timeout can trigger legacy fallback");
    try {state.requests.abandon(state.expected_id);return state.legacy();}catch(...){state.phase=Impl::Phase::failed;throw;}
}
void McpHandshake::initialized_sent() {auto& state=*impl_;if(state.phase!=Impl::Phase::ack)throw McpProtocolError("MCP initialization acknowledgement is not pending");state.phase=Impl::Phase::legacy_ready;}
bool McpHandshake::ready() const {return impl_->phase==Impl::Phase::modern_ready || impl_->phase==Impl::Phase::legacy_ready;}
const McpServerDescription& McpHandshake::server() const {if(!ready())throw McpProtocolError("MCP negotiation is not ready");return impl_->description;}
}
