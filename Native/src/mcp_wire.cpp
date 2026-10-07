#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
#include <set>
#include <vector>
#include <utility>

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr std::size_t message_limit=1024*1024;
Json parse(std::string_view bytes) {
    if(bytes.empty() || bytes.size()>message_limit) throw McpProtocolError("MCP message exceeds byte limits");
    std::vector<std::set<std::string>> fields;
    try {
        return Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& parsed) {
            if(depth>64) throw McpProtocolError("MCP JSON nesting exceeds limits");
            if(event==Json::parse_event_t::object_start) fields.emplace_back();
            else if(event==Json::parse_event_t::object_end) fields.pop_back();
            else if(event==Json::parse_event_t::key && !fields.back().insert(parsed.get<std::string>()).second) throw McpProtocolError("Duplicate MCP JSON field");
            return true;
        });
    } catch(const Json::exception&) {throw McpProtocolError("Invalid MCP JSON or UTF-8");}
}
void text(const std::string& value,std::size_t limit,const char* error) {
    if(value.empty() || value.size()>limit || value.find('\0')!=std::string::npos) throw McpProtocolError(error);
}
std::string id_json(const Json& value,bool diagnostic=false) {
    if(value.is_null() && diagnostic) return "null";
    if(value.is_string()) {text(value.get<std::string>(),128,"Invalid MCP request ID");return value.dump();}
    if(!value.is_number_integer()) throw McpProtocolError("MCP IDs must be strings or integers");
    return value.dump();
}
McpWireMessage decode(const std::string& bytes) {
    const auto value=parse(bytes);
    if(!value.is_object() || !value.contains("jsonrpc") || value["jsonrpc"]!="2.0") throw McpProtocolError("Invalid MCP JSON-RPC envelope");
    const bool method=value.contains("method"),result=value.contains("result"),error=value.contains("error");
    if(static_cast<int>(method)+static_cast<int>(result)+static_cast<int>(error)!=1) throw McpProtocolError("Ambiguous MCP message kind");
    McpWireMessage message;message.raw_json=bytes;
    if(method) {
        if(!value["method"].is_string()) throw McpProtocolError("Invalid MCP method");
        message.method=value["method"].get<std::string>();text(message.method,256,"Invalid MCP method");
        if(value.contains("params") && !value["params"].is_object()) throw McpProtocolError("MCP parameters must be an object");
        message.payload_json=value.contains("params")?*mcp_object_member(bytes,"params"):"{}";
        if(value.contains("id")) {message.kind=McpMessageKind::request;message.id_json=id_json(value["id"]);}
        else message.kind=McpMessageKind::notification;
    } else {
        if(value.contains("params")) throw McpProtocolError("MCP responses cannot contain request parameters");
        if(result) {
            if(!value.contains("id") || !value["result"].is_object()) throw McpProtocolError("Invalid MCP result response");
            message.kind=McpMessageKind::result;message.id_json=id_json(value["id"]);message.payload_json=*mcp_object_member(bytes,"result");
        } else {
            const auto& failure=value["error"];
            if(!failure.is_object() || !failure.contains("code") || !failure["code"].is_number_integer() || !failure.contains("message") || !failure["message"].is_string()) throw McpProtocolError("Invalid MCP error response");
            const auto description=failure["message"].get<std::string>();
            if(description.size()>65536 || description.find('\0')!=std::string::npos) throw McpProtocolError("MCP error description exceeds limits");
            message.kind=McpMessageKind::error;message.payload_json=*mcp_object_member(bytes,"error");
            if(value.contains("id")) message.id_json=id_json(value["id"],true);
        }
    }
    return message;
}
Json parameters(std::string_view source) {
    auto value=parse(source);if(!value.is_object()) throw McpProtocolError("MCP parameters must be an object");return value;
}
void whitespace(std::string_view source,std::size_t& cursor) {
    while(cursor<source.size() && (source[cursor]==' ' || source[cursor]=='\t' || source[cursor]=='\r' || source[cursor]=='\n'))++cursor;
}
std::string raw_value(std::string_view source,std::size_t& cursor) {
    whitespace(source,cursor);const auto begin=cursor;std::size_t depth=0;bool quoted=false,escaped=false;
    for(;cursor<source.size();++cursor) {
        const auto value=source[cursor];
        if(quoted){if(escaped)escaped=false;else if(value=='\\')escaped=true;else if(value=='"')quoted=false;continue;}
        if(value=='"')quoted=true;
        else if(value=='{' || value=='[')++depth;
        else if(value=='}' || value==']'){if(!depth)break;--depth;}
        else if(value==',' && !depth)break;
    }
    auto end=cursor;while(end>begin && (source[end-1]==' ' || source[end-1]=='\t' || source[end-1]=='\r' || source[end-1]=='\n'))--end;
    return std::string(source.substr(begin,end-begin));
}
std::string frame(const Json& value) {
    try {
        auto encoded=value.dump();
        if(encoded.size()>message_limit) throw McpProtocolError("Encoded MCP message exceeds byte limits");
        // Validate the complete envelope too: metadata wrapping counts toward
        // depth limits, not only the caller's standalone parameter object.
        decode(encoded);encoded.push_back('\n');return encoded;
    } catch(const Json::exception&) {throw McpProtocolError("Invalid outgoing MCP UTF-8");}
}
}
struct McpLineStream::Impl {
    Sink sink;
    std::string pending;
    bool failed=false,closed=false;
    explicit Impl(Sink callback):sink(std::move(callback)) {if(!sink) throw std::invalid_argument("MCP stream requires a sink");}
};
McpLineStream::McpLineStream(Sink sink):impl_(std::make_unique<Impl>(std::move(sink))) {}
McpLineStream::~McpLineStream()=default;
void McpLineStream::feed(std::string_view bytes) {
    auto& state=*impl_;
    if(state.failed || state.closed) throw McpProtocolError("MCP stream is no longer usable");
    try {
        while(!bytes.empty()) {
            const auto newline=bytes.find('\n');const auto count=newline==std::string_view::npos?bytes.size():newline;
            if(count>message_limit-state.pending.size()) throw McpProtocolError("MCP line exceeds byte limits");
            state.pending.append(bytes.data(),count);
            if(newline==std::string_view::npos) break;
            if(!state.pending.empty() && state.pending.back()=='\r') state.pending.pop_back();
            const auto message=decode(state.pending);state.pending.clear();state.sink(message);
            bytes.remove_prefix(count+1);
        }
    } catch(...) {state.failed=true;state.pending.clear();throw;}
}
void McpLineStream::finish() {
    auto& state=*impl_;if(state.failed) throw McpProtocolError("MCP stream failed");
    if(!state.pending.empty()) {state.failed=true;state.pending.clear();throw McpProtocolError("Incomplete MCP line at EOF");}
    state.closed=true;
}
std::string mcp_request(std::string id,std::string method,std::string_view source,McpWireEra era,const McpClientIdentity& identity) {
    text(id,128,"Invalid outgoing MCP request ID");text(method,256,"Invalid outgoing MCP method");
    auto params=parameters(source);
    auto encoded_params=mcp_compact_object(source);
    if(era==McpWireEra::modern) {
        if(params.contains("_meta")) throw McpProtocolError("Modern MCP metadata is owned by the backend");
        text(identity.name,128,"Invalid MCP client identity");text(identity.version,64,"Invalid MCP client version");
        Json metadata={{"io.modelcontextprotocol/protocolVersion","2026-07-28"},
            {"io.modelcontextprotocol/clientInfo",{{"name",identity.name},{"version",identity.version}}},
            {"io.modelcontextprotocol/clientCapabilities",parameters(identity.capabilities_json)}};
        encoded_params.pop_back();
        if(!params.empty())encoded_params.push_back(',');
        try {encoded_params+="\"_meta\":"+metadata.dump()+"}";}
        catch(const Json::exception&) {throw McpProtocolError("Invalid outgoing MCP UTF-8");}
    }
    try {
        auto encoded="{\"jsonrpc\":\"2.0\",\"id\":"+Json(id).dump()+",\"method\":"+Json(method).dump()+",\"params\":"+encoded_params+"}";
        decode(encoded);encoded.push_back('\n');return encoded;
    }catch(const Json::exception&) {throw McpProtocolError("Invalid outgoing MCP UTF-8");}
}
std::string mcp_notification(std::string method,std::string_view source) {
    text(method,256,"Invalid outgoing MCP notification method");
    return frame(Json{{"jsonrpc","2.0"},{"method",method},{"params",parameters(source)}});
}
std::string mcp_peer_reply(const McpWireMessage& request,McpWireEra era) {
    if(era!=McpWireEra::legacy) throw McpProtocolError("Modern MCP peers cannot send client requests");
    // Revalidate the original envelope. Public structs cannot bypass framing,
    // ID precision or envelope checks by supplying fabricated parsed fields.
    const auto decoded=decode(request.raw_json);
    if(decoded.kind!=McpMessageKind::request || request.kind!=decoded.kind ||
        request.id_json!=decoded.id_json || request.method!=decoded.method ||
        request.payload_json!=decoded.payload_json) throw McpProtocolError("Invalid MCP peer request");
    Json reply{{"jsonrpc","2.0"},{"id",parse(decoded.id_json)}};
    if(decoded.method=="ping") reply["result"]=Json::object();
    else reply["error"]={{"code",-32601},{"message","Client method is not supported"}};
    return frame(reply);
}
std::string mcp_compact_object(std::string_view source) {
    parameters(source);std::string result;result.reserve(source.size());
    bool quoted=false,escaped=false;
    for(const char value:source) {
        if(quoted) {
            result.push_back(value);
            if(escaped)escaped=false;
            else if(value=='\\')escaped=true;
            else if(value=='"')quoted=false;
        }else if(value=='"') {quoted=true;result.push_back(value);}
        else if(value!=' ' && value!='\t' && value!='\r' && value!='\n')result.push_back(value);
    }
    return result;
}
std::optional<std::string> mcp_object_member(std::string_view source,std::string_view key) {
    parameters(source);std::size_t cursor=0;whitespace(source,cursor);++cursor;whitespace(source,cursor);
    while(source[cursor]!='}') {
        const auto begin=cursor++;bool escaped=false;
        while(cursor<source.size()) {const auto value=source[cursor++];if(escaped)escaped=false;else if(value=='\\')escaped=true;else if(value=='"')break;}
        const auto name=Json::parse(source.substr(begin,cursor-begin)).get<std::string>();
        whitespace(source,cursor);++cursor;auto value=raw_value(source,cursor);
        if(name==key)return value;
        whitespace(source,cursor);if(source[cursor]=='}')break;++cursor;whitespace(source,cursor);
    }
    return std::nullopt;
}
std::vector<std::string> mcp_array_values(std::string_view source) {
    if(!parse(source).is_array())throw McpProtocolError("MCP collection must be an array");
    std::vector<std::string> result;std::size_t cursor=0;whitespace(source,cursor);++cursor;whitespace(source,cursor);
    while(source[cursor]!=']') {
        if(result.size()>=128)throw McpProtocolError("MCP collection exceeds limits");
        result.push_back(raw_value(source,cursor));whitespace(source,cursor);
        if(source[cursor]==']')break;++cursor;whitespace(source,cursor);
    }
    return result;
}
}
