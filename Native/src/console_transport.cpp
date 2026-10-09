#include "agentflow/console_transport.hpp"
#include "nlohmann/json.hpp"
#include <stdexcept>
#include <set>
#include <vector>
namespace agentflow {
ConsoleTransport::ConsoleTransport(int port,const std::string& workspace):client_("127.0.0.1",port){
    if(!workspace.empty()){
#if defined(_WIN32)
        workspace_=std::make_unique<WorkspaceTools>(workspace);
        workspace_id_=workspace_->identity();root_=workspace_->root_path();
#else
        throw std::invalid_argument("Console workspace pinning requires a supported local workspace implementation");
#endif
    }
}
void ConsoleTransport::verify(const httplib::Headers& headers){
    if(workspace_id_.empty())return;
#if defined(_WIN32)
    if(workspace_->identity()!=workspace_id_||workspace_->root_path()!=root_)
        throw std::runtime_error("Selected console workspace changed; reconnect explicitly");
#endif
    const auto response=client_.Get("/v1/workspace",headers);
    if(!response||response->status!=200||response->body.size()>131072)
        throw std::runtime_error("Cannot verify the selected console workspace; no command was sent");
    using Json=nlohmann::json;std::vector<std::set<std::string>> keys;
    const auto value=Json::parse(response->body,[&](int depth,Json::parse_event_t event,Json& parsed){
        if(depth>4)throw std::runtime_error("Invalid workspace metadata nesting");
        if(event==Json::parse_event_t::object_start)keys.emplace_back();
        else if(event==Json::parse_event_t::object_end)keys.pop_back();
        else if(event==Json::parse_event_t::key&&!keys.back().insert(parsed.get<std::string>()).second)
            throw std::runtime_error("Duplicate workspace metadata field; no command was sent");
        return true;
    });
    if(!value.is_object()||value.size()!=4||!value.contains("configured")||value.at("configured")!=true||
        !value.contains("workspace_id")||!value.at("workspace_id").is_string()||
        !value.contains("root")||!value.at("root").is_string()||
        !value.contains("authority_id")||!value.at("authority_id").is_string()||
        value.at("workspace_id")!=workspace_id_||value.at("root")!=root_)
        throw std::runtime_error("Backend workspace differs from the selected console workspace; no command was sent");
    const auto authority=value.at("authority_id").get<std::string>();
    if(authority.size()!=32||authority.find_first_not_of("0123456789abcdef")!=std::string::npos)
        throw std::runtime_error("Invalid backend workspace authority; no command was sent");
    if(authority_.empty())authority_=authority;
    else if(authority_!=authority)
        throw std::runtime_error("Backend workspace authority changed; reconnect explicitly. No command was sent");
}
httplib::Result ConsoleTransport::Get(const std::string& route,const httplib::Headers& headers){
    verify(headers);return client_.Get(route,headers);
}
httplib::Result ConsoleTransport::Post(const std::string& route,const httplib::Headers& headers,const std::string& body,const std::string& content_type){
    verify(headers);
    if(!workspace_id_.empty()&&(route=="/v1/runs"||route=="/v1/graph-runs")){
        auto input=nlohmann::json::parse(body);if(!input.is_object())throw std::invalid_argument("Console run request must be an object");
        input["expected_workspace_id"]=workspace_id_;input["expected_workspace_authority_id"]=authority_;
        return client_.Post(route,headers,input.dump(),content_type);
    }
    return client_.Post(route,headers,body,content_type);
}
}
