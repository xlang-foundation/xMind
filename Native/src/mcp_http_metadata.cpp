#include "agentflow/mcp_http_metadata.hpp"
#include "nlohmann/json.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <utility>

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr std::size_t header_limit=16*1024,total_limit=64*1024;
[[noreturn]] void invalid() {throw McpProtocolError("Invalid MCP HTTP metadata");}
Json object(std::string_view source) {return Json::parse(mcp_compact_object(source));}
bool token(std::string_view value) {
    if(value.empty() || value.size()>128)return false;
    for(unsigned char c:value) if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(c))!=std::string_view::npos))return false;
    return true;
}
std::string folded(std::string value) {for(auto& c:value)if(c>='A' && c<='Z')c=static_cast<char>(c-'A'+'a');return value;}
std::string encoded(std::string_view value) {
    if(value.size()>header_limit)invalid();
    bool plain=!(value.starts_with("=?base64?") && value.ends_with("?="));
    if(!value.empty() && (value.front()==' ' || value.front()=='\t' || value.back()==' ' || value.back()=='\t'))plain=false;
    for(unsigned char c:value)if(c!='\t' && (c<0x20 || c>0x7e))plain=false;
    if(plain)return std::string(value);
    constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result="=?base64?";
    for(std::size_t i=0;i<value.size();i+=3) {
        const auto a=static_cast<unsigned char>(value[i]);
        const auto b=i+1<value.size()?static_cast<unsigned char>(value[i+1]):0;
        const auto c=i+2<value.size()?static_cast<unsigned char>(value[i+2]):0;
        result+=alphabet[a>>2];result+=alphabet[((a&3)<<4)|(b>>4)];
        result+=i+1<value.size()?alphabet[((b&15)<<2)|(c>>6)]:'=';
        result+=i+2<value.size()?alphabet[c&63]:'=';
    }
    result+="?=";if(result.size()>header_limit)invalid();return result;
}
void add(std::vector<McpHttpHeader>& headers,std::string name,std::string value) {
    std::size_t total=name.size()+value.size();
    for(const auto& h:headers)total+=h.name.size()+h.value.size();
    if(value.size()>header_limit || total>total_limit || headers.size()>=67)invalid();
    headers.push_back({std::move(name),std::move(value)});
}
std::string string(const Json& value) {if(!value.is_string())invalid();return value.get<std::string>();}
// Convert an exact decimal token, including exponent notation, without a
// double round-trip. JSON validation already checked its lexical grammar.
std::string integer(std::string_view raw) {
    std::size_t i=0;const bool negative=raw.front()=='-';if(negative)++i;
    std::string digits;std::int64_t fraction=0;bool decimal=false;
    for(;i<raw.size() && raw[i]!='e' && raw[i]!='E';++i) {
        if(raw[i]=='.'){decimal=true;continue;}
        digits+=raw[i];if(decimal)++fraction;
    }
    std::int64_t exponent=0;bool exponent_negative=false;
    if(i<raw.size()) {
        ++i;if(raw[i]=='+' || raw[i]=='-'){exponent_negative=raw[i]=='-';++i;}
        for(;i<raw.size();++i){if(exponent>1000000)invalid();exponent=exponent*10+(raw[i]-'0');}
    }
    if(exponent_negative)exponent=-exponent;
    const auto first=digits.find_first_not_of('0');if(first==std::string::npos)return "0";
    digits.erase(0,first);const auto scale=exponent-fraction;
    if(scale<0) {
        const auto remove=static_cast<std::uint64_t>(-scale);
        if(remove>=digits.size())invalid();
        for(std::size_t p=digits.size()-static_cast<std::size_t>(remove);p<digits.size();++p)if(digits[p]!='0')invalid();
        digits.resize(digits.size()-static_cast<std::size_t>(remove));
    } else {
        if(scale>16 || digits.size()+static_cast<std::size_t>(scale)>16)invalid();
        digits.append(static_cast<std::size_t>(scale),'0');
    }
    if(digits.size()>16 || (digits.size()==16 && digits>"9007199254740991"))invalid();
    return negative?"-"+digits:digits;
}
}
std::vector<McpHttpHeader> mcp_http_request_headers(std::string_view request,McpWireEra era,std::string_view legacy_protocol) {
    const auto value=object(request);
    if(!value.contains("jsonrpc") || value["jsonrpc"]!="2.0" || !value.contains("method") || value.contains("result") || value.contains("error"))invalid();
    const auto method=string(value["method"]);if(method.empty() || method.size()>256)invalid();
    for(unsigned char c:method)if(c<0x21 || c>0x7e)invalid();
    Json params=Json::object();if(value.contains("params")){params=value["params"];if(!params.is_object())invalid();}
    std::string protocol;
    if(era==McpWireEra::modern) {
        if(!params.contains("_meta") || !params["_meta"].is_object() || !params["_meta"].contains("io.modelcontextprotocol/protocolVersion"))invalid();
        protocol=string(params["_meta"]["io.modelcontextprotocol/protocolVersion"]);
        if(protocol!="2026-07-28")invalid();
    } else {
        if(legacy_protocol!="2025-11-25" && legacy_protocol!="2025-06-18")invalid();
        protocol=std::string(legacy_protocol);
    }
    std::vector<McpHttpHeader> result;add(result,"MCP-Protocol-Version",protocol);
    if(era==McpWireEra::modern) {
        add(result,"Mcp-Method",method);
        if(method=="tools/call" || method=="prompts/get" || method=="resources/read") {
            const auto key=method=="resources/read"?"uri":"name";
            if(!params.contains(key))invalid();const auto name=string(params[key]);if(name.empty())invalid();
            add(result,"Mcp-Name",encoded(name));
        }
    }
    return result;
}
McpHttpToolHeaders::McpHttpToolHeaders(std::string_view source) {
    const auto schema=object(source);std::set<std::string> names;
    std::function<void(const Json&,std::vector<std::string>,bool)> visit;
    visit=[&](const Json& node,std::vector<std::string> path,bool reachable) {
        if(node.is_array()){for(const auto& item:node)visit(item,path,false);return;}
        if(!node.is_object())return;
        if(node.contains("x-mcp-header")) {
            if(!reachable || path.empty() || !node.contains("type"))invalid();
            const auto name=string(node["x-mcp-header"]),type=string(node["type"]);
            if(!token(name) || !names.insert(folded(name)).second || (type!="string" && type!="integer" && type!="boolean") || parameters_.size()>=64)invalid();
            parameters_.push_back({path,"Mcp-Param-"+name,type});
        }
        for(auto it=node.begin();it!=node.end();++it) {
            if(it.key()=="properties" && it.value().is_object()) {
                for(auto p=it.value().begin();p!=it.value().end();++p){auto next=path;next.push_back(p.key());visit(p.value(),std::move(next),reachable);}
            } else if(it.key()!="x-mcp-header")visit(it.value(),path,false);
        }
    };
    visit(schema,{},true);
}
std::vector<McpHttpHeader> McpHttpToolHeaders::project(std::string_view arguments) const {
    auto root=mcp_compact_object(arguments);std::vector<McpHttpHeader> result;
    for(const auto& parameter:parameters_) {
        std::optional<std::string> raw=root;
        for(const auto& key:parameter.path) {
            if(!raw || *raw=="null"){raw.reset();break;}
            if(raw->empty() || raw->front()!='{')invalid();
            raw=mcp_object_member(*raw,key);
        }
        if(!raw || *raw=="null")continue;
        const auto value=Json::parse(*raw);std::string text;
        if(parameter.type=="string")text=string(value);
        else if(parameter.type=="boolean"){if(!value.is_boolean())invalid();text=value.get<bool>()?"true":"false";}
        else {if(!value.is_number())invalid();text=integer(*raw);}
        add(result,parameter.name,encoded(text));
    }
    return result;
}
}
