#pragma once
#include "agentflow/mcp_handshake.hpp"
#include "agentflow/mcp_transport.hpp"
#include <optional>
#include <functional>
#include <map>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace agentflow {
struct McpToolDescription {
    std::string name,description,input_schema_json,annotations_json;
    std::optional<std::string> output_schema_json;
};
struct McpInputRequired {
    // Each value is the raw, validated embedded request object keyed by the
    // peer's opaque response identity. request_state is opaque and must only
    // be echoed by a continuation owner; it is not interpreted here.
    std::map<std::string,std::string> input_requests_json;
    std::optional<std::string> request_state;
};
struct McpToolReply {std::string request_id,response_json,result_json;bool is_error=false;std::optional<McpInputRequired> input_required;};
struct McpDispatchFailure : McpTransportError {
    bool possibly_sent;
    std::string request_id,response_json;
    McpDispatchFailure(bool sent,std::string id,std::string response):McpTransportError("MCP tool dispatch did not complete"),possibly_sent(sent),request_id(std::move(id)),response_json(std::move(response)) {}
};
struct McpToolPage {
    std::vector<McpToolDescription> tools;
    std::optional<std::string> next_cursor;
};
struct McpMetadataPage {
    // Each entry is a validated MCP Resource or Prompt object. Preserve the
    // exact peer JSON for fingerprinting and durable approval binding.
    std::vector<std::string> entries_json;
    std::optional<std::string> next_cursor;
};
struct McpReadOnlyReply {std::string request_id,response_json,result_json;};
struct McpSubscriptionFilter {
    bool tools_list_changed=false,prompts_list_changed=false,resources_list_changed=false;
    std::vector<std::string> resource_subscriptions;
};
struct McpSubscriptionAcknowledgement {
    std::string subscription_id;
    McpSubscriptionFilter notifications;
};
class McpToolRegistry;
// One native access owner; discovery is metadata, not execution permission.
// All transport bindings dispatch only through the same journal-backed registry.
// A lost reply must preserve possibly_sent; changing transport cannot replay it.
class McpToolClient {
public:
    using Deadline=McpDeadline;
    virtual ~McpToolClient()=default;
    virtual McpToolPage list_tools(std::optional<std::string> cursor,Deadline deadline,
        std::stop_token cancel={})=0;
    virtual McpMetadataPage list_resources(std::optional<std::string>,Deadline,std::stop_token={}) {
        throw McpProtocolError("MCP resource discovery is not implemented by this client");
    }
    virtual McpMetadataPage list_resource_templates(std::optional<std::string>,Deadline,std::stop_token={}) {
        throw McpProtocolError("MCP resource-template discovery is not implemented by this client");
    }
    virtual McpReadOnlyReply read_resource(const std::string&,Deadline,std::stop_token={}) {
        throw McpProtocolError("MCP resource reads are not implemented by this client");
    }
    virtual McpMetadataPage list_prompts(std::optional<std::string>,Deadline,std::stop_token={}) {
        throw McpProtocolError("MCP prompt discovery is not implemented by this client");
    }
    virtual McpReadOnlyReply get_prompt(const std::string&,std::string_view,Deadline,std::stop_token={}) {
        throw McpProtocolError("MCP prompt retrieval is not implemented by this client");
    }
    // Long-lived modern-protocol request. A separate access owner should be
    // used when the caller also needs to make ordinary requests concurrently.
    virtual McpSubscriptionAcknowledgement listen_subscriptions(const McpSubscriptionFilter&,
        const std::function<void(const McpWireMessage&)>&,Deadline,std::stop_token={},
        const std::function<void(const McpSubscriptionAcknowledgement&)>& on_acknowledgement={}) {
        throw McpProtocolError("MCP subscriptions/listen is not implemented by this client");
    }
    virtual bool ready() const=0;
    virtual const McpServerDescription& server() const=0;
    virtual void shutdown() noexcept=0;
private:
    friend class McpToolRegistry;
    virtual McpToolReply call_tool(const std::string& name,std::string_view arguments,
        Deadline deadline,std::stop_token cancel)=0;
};
}
