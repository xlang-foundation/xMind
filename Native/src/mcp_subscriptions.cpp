#include "agentflow/mcp_subscriptions.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <set>

namespace agentflow {
namespace {
using Json=nlohmann::json;
std::vector<std::string> checked_uris(const McpSubscriptionFilter& filter) {
    if(filter.resource_subscriptions.size()>128)throw McpProtocolError("MCP resource subscription count exceeds limits");
    auto uris=filter.resource_subscriptions;
    for(const auto& uri:uris)if(uri.empty()||uri.size()>8192||uri.find('\0')!=std::string::npos)throw McpProtocolError("Invalid MCP resource subscription URI");
    std::sort(uris.begin(),uris.end());
    if(std::adjacent_find(uris.begin(),uris.end())!=uris.end())throw McpProtocolError("Duplicate MCP resource subscription URI");
    if(!filter.tools_list_changed&&!filter.prompts_list_changed&&!filter.resources_list_changed&&uris.empty())throw McpProtocolError("MCP subscriptions/listen requires at least one notification filter");
    return uris;
}
bool has_subscription_id(const Json& metadata,const std::string& id) {
    return metadata.is_object()&&metadata.contains("io.modelcontextprotocol/subscriptionId")&&metadata.at("io.modelcontextprotocol/subscriptionId")==id;
}
bool advertised(const Json& capabilities,const char* section,const char* key) {
    return capabilities.contains(section)&&capabilities.at(section).is_object()&&capabilities.at(section).contains(key)&&capabilities.at(section).at(key).is_boolean()&&capabilities.at(section).at(key).get<bool>();
}
}
std::string mcp_subscription_request_params(const McpSubscriptionFilter& requested) {
    const auto uris=checked_uris(requested);Json filters=Json::object();
    if(requested.tools_list_changed)filters["toolsListChanged"]=true;
    if(requested.prompts_list_changed)filters["promptsListChanged"]=true;
    if(requested.resources_list_changed)filters["resourcesListChanged"]=true;
    if(!uris.empty())filters["resourceSubscriptions"]=uris;
    return Json{{"notifications",std::move(filters)}}.dump();
}
McpSubscriptionTracker::McpSubscriptionTracker(McpSubscriptionFilter requested,const McpServerDescription& server,std::string request_id)
    :requested_(std::move(requested)),server_(server),request_id_(std::move(request_id)) {
    (void)checked_uris(requested_);
    if(server_.era!=McpWireEra::modern||server_.protocol_version!="2026-07-28")throw McpProtocolError("MCP subscriptions/listen requires protocol 2026-07-28");
    if(request_id_.empty()||request_id_.size()>256)throw McpProtocolError("Invalid MCP subscription request identity");
}
void McpSubscriptionTracker::notification(const McpWireMessage& message) {
    if(message.kind!=McpMessageKind::notification)throw McpProtocolError("MCP subscription stream received a non-notification event");
    const auto params=Json::parse(message.payload_json);
    if(!acknowledgement_) {
        if(message.method!="notifications/subscriptions/acknowledged")throw McpProtocolError("MCP subscription notification arrived before its acknowledgement");
        if(!params.is_object()||!params.contains("_meta")||!has_subscription_id(params.at("_meta"),request_id_)||!params.contains("notifications")||!params.at("notifications").is_object())throw McpProtocolError("Invalid MCP subscription acknowledgement");
        const auto& accepted=params.at("notifications");
        for(auto it=accepted.begin();it!=accepted.end();++it)if(it.key()!="toolsListChanged"&&it.key()!="promptsListChanged"&&it.key()!="resourcesListChanged"&&it.key()!="resourceSubscriptions")throw McpProtocolError("MCP subscription acknowledgement contains an unknown filter");
        McpSubscriptionFilter result;
        const auto flag=[&](const char* key,bool asked){if(!accepted.contains(key))return false;if(!accepted.at(key).is_boolean()||!accepted.at(key).get<bool>()||!asked)throw McpProtocolError("MCP server acknowledged an unrequested subscription filter");return true;};
        result.tools_list_changed=flag("toolsListChanged",requested_.tools_list_changed);
        result.prompts_list_changed=flag("promptsListChanged",requested_.prompts_list_changed);
        result.resources_list_changed=flag("resourcesListChanged",requested_.resources_list_changed);
        if(accepted.contains("resourceSubscriptions")) {
            const auto& values=accepted.at("resourceSubscriptions");const auto uris=checked_uris(requested_);
            if(!values.is_array()||values.size()>uris.size())throw McpProtocolError("Invalid acknowledged MCP resource subscription list");
            std::set<std::string> seen;
            for(const auto& value:values) {
                if(!value.is_string())throw McpProtocolError("Invalid acknowledged MCP resource subscription URI");
                const auto uri=value.get<std::string>();
                if(!std::binary_search(uris.begin(),uris.end(),uri)||!seen.insert(uri).second)throw McpProtocolError("MCP server acknowledged an unrequested resource subscription");
                result.resource_subscriptions.push_back(uri);
            }
        }
        const auto capabilities=Json::parse(server_.capabilities_json);
        if(result.tools_list_changed&&!advertised(capabilities,"tools","listChanged"))throw McpProtocolError("MCP server acknowledged an unadvertised tools-list subscription");
        if(result.prompts_list_changed&&!advertised(capabilities,"prompts","listChanged"))throw McpProtocolError("MCP server acknowledged an unadvertised prompts-list subscription");
        if(result.resources_list_changed&&!advertised(capabilities,"resources","listChanged"))throw McpProtocolError("MCP server acknowledged an unadvertised resources-list subscription");
        if(!result.resource_subscriptions.empty()&&!advertised(capabilities,"resources","subscribe"))throw McpProtocolError("MCP server acknowledged unadvertised resource subscriptions");
        acknowledgement_=McpSubscriptionAcknowledgement{request_id_,std::move(result)};
        return;
    }
    if(message.method=="notifications/subscriptions/acknowledged")throw McpProtocolError("Duplicate MCP subscription acknowledgement");
    if(!params.is_object()||!params.contains("_meta")||!has_subscription_id(params.at("_meta"),request_id_))throw McpProtocolError("MCP subscription notification has the wrong subscription ID");
    const auto& accepted=acknowledgement_->notifications;
    if(message.method=="notifications/tools/list_changed") {if(!accepted.tools_list_changed)throw McpProtocolError("MCP server sent an unrequested tools-list notification");}
    else if(message.method=="notifications/prompts/list_changed") {if(!accepted.prompts_list_changed)throw McpProtocolError("MCP server sent an unrequested prompts-list notification");}
    else if(message.method=="notifications/resources/list_changed") {if(!accepted.resources_list_changed)throw McpProtocolError("MCP server sent an unrequested resources-list notification");}
    else if(message.method=="notifications/resources/updated") {
        if(!params.contains("uri")||!params.at("uri").is_string()||std::find(accepted.resource_subscriptions.begin(),accepted.resource_subscriptions.end(),params.at("uri").get<std::string>())==accepted.resource_subscriptions.end())throw McpProtocolError("MCP server sent an unrequested resource update");
    } else throw McpProtocolError("MCP server sent an unsupported subscription notification");
}
void McpSubscriptionTracker::complete(const McpWireMessage& response) {
    if(!acknowledgement_)throw McpProtocolError("MCP subscription stream closed before acknowledgement");
    if(response.kind!=McpMessageKind::result)throw McpProtocolError("MCP subscription completion is not a result");
    const auto result=Json::parse(response.payload_json);
    if(!result.is_object()||!result.contains("resultType")||result.at("resultType")!="complete"||!result.contains("_meta")||!has_subscription_id(result.at("_meta"),request_id_))throw McpProtocolError("Invalid MCP graceful subscription completion");
}
const std::optional<McpSubscriptionAcknowledgement>& McpSubscriptionTracker::acknowledgement() const noexcept{return acknowledgement_;}
}
