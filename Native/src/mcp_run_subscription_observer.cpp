#include "agentflow/mcp_run_subscription_observer.hpp"
#include "agentflow/mcp_subscriptions.hpp"
#include "nlohmann/json.hpp"
#include <stdexcept>
#include <utility>

namespace agentflow {
namespace {
using Json=nlohmann::json;
Json filter_json(const McpSubscriptionFilter& filter) {
    return Json{{"tools_list_changed",filter.tools_list_changed},
        {"prompts_list_changed",filter.prompts_list_changed},
        {"resources_list_changed",filter.resources_list_changed}};
}
}

std::optional<McpSubscriptionFilter> mcp_run_subscription_filter(const McpServerDescription& server) {
    if(server.era!=McpWireEra::modern||server.protocol_version!="2026-07-28")return std::nullopt;
    const auto capabilities=Json::parse(server.capabilities_json);
    const auto advertises=[&](const char* group) {
        return capabilities.contains(group)&&capabilities.at(group).is_object()&&
            capabilities.at(group).contains("listChanged")&&
            capabilities.at(group).at("listChanged").is_boolean()&&
            capabilities.at(group).at("listChanged").get<bool>();
    };
    McpSubscriptionFilter filter;
    filter.tools_list_changed=advertises("tools");
    filter.prompts_list_changed=advertises("prompts");
    filter.resources_list_changed=advertises("resources");
    if(!filter.tools_list_changed&&!filter.prompts_list_changed&&!filter.resources_list_changed)return std::nullopt;
    return filter;
}

McpRunSubscriptionObserver::McpRunSubscriptionObserver(PersistenceService& store,std::string run,
    std::string server,std::int64_t revision,std::unique_ptr<McpToolClient> client,
    McpSubscriptionFilter filter,McpToolClient::Deadline deadline,
    std::shared_ptr<std::atomic<bool>> catalogue_invalidated)
    :store_(store),run_(std::move(run)),server_(std::move(server)),revision_(revision),
     client_(std::move(client)),filter_(std::move(filter)),deadline_(deadline),
     catalogue_invalidated_(std::move(catalogue_invalidated)) {}

McpRunSubscriptionObserver::~McpRunSubscriptionObserver(){stop();}

void McpRunSubscriptionObserver::start() {
    if(worker_.joinable())throw std::logic_error("MCP subscription observer already started");
    persist("mcp.subscription.started",Json{{"filter",filter_json(filter_)}}.dump());
    try {worker_=std::jthread([this](std::stop_token stop){observe(stop);});}
    catch(...) {
        if(catalogue_invalidated_)catalogue_invalidated_->store(true,std::memory_order_release);
        try{persist("mcp.subscription.closed",Json{{"outcome","failed"},{"reason","listener_start_failed"}}.dump());}catch(...){}
        throw;
    }
}

void McpRunSubscriptionObserver::stop() noexcept {
    if(worker_.joinable()){worker_.request_stop();worker_.join();}
    if(client_)client_->shutdown();
}

void McpRunSubscriptionObserver::persist(const char* kind,std::string data_json) {
    auto data=Json::parse(data_json);
    if(!data.is_object())throw std::invalid_argument("MCP subscription event details must be an object");
    data["server_id"]=server_;data["config_revision"]=revision_;
    store_.append_event(run_,kind,data.dump()).get();
}

void McpRunSubscriptionObserver::observe(std::stop_token stop) noexcept {
    std::size_t notifications=0;bool overflow_recorded=false;
    try {
        const auto accepted=client_->listen_subscriptions(filter_,[&](const McpWireMessage& message) {
            const bool refresh=message.method=="notifications/tools/list_changed"||
                message.method=="notifications/prompts/list_changed"||
                message.method=="notifications/resources/list_changed";
            if(refresh&&catalogue_invalidated_)catalogue_invalidated_->store(true,std::memory_order_release);
            if(notifications>=64) {
                if(!overflow_recorded){overflow_recorded=true;persist("mcp.subscription.overflow",Json{{"limit",64}}.dump());}
                return;
            }
            ++notifications;
            // Peer payloads and resource URIs may contain secrets.
            persist("mcp.subscription.notification",Json{{"method",message.method},
                {"refresh_next_run",refresh},{"catalogue_invalidated",refresh}}.dump());
        },deadline_,stop,[&](const McpSubscriptionAcknowledgement& accepted) {
            persist("mcp.subscription.acknowledged",Json{{"filter",filter_json(accepted.notifications)}}.dump());
        });
        (void)accepted;
        const bool unexpected=!stop.stop_requested();
        if(unexpected&&catalogue_invalidated_)catalogue_invalidated_->store(true,std::memory_order_release);
        persist("mcp.subscription.closed",Json{{"outcome","completed"},{"notification_count",notifications},
            {"catalogue_invalidated",unexpected}}.dump());
    } catch(...) {
        const bool unexpected=!stop.stop_requested();
        if(unexpected&&catalogue_invalidated_)catalogue_invalidated_->store(true,std::memory_order_release);
        try {
            persist("mcp.subscription.closed",Json{{"outcome",stop.stop_requested()?"stopped":"failed"},
                {"reason",stop.stop_requested()?"run_ended":"peer_or_protocol_failure"},
                {"notification_count",notifications},{"catalogue_invalidated",unexpected}}.dump());
        } catch(...) {}
    }
}

} // namespace agentflow
