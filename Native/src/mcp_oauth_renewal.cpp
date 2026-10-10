#include "agentflow/mcp_oauth_renewal.hpp"
#include "agentflow/mcp_http_client.hpp"
#include "agentflow/mcp_wire.hpp"
#include <algorithm>
#include <chrono>
namespace agentflow {
McpOAuthRefreshRecord renew_mcp_oauth_grant(PersistenceService& store,const McpServerSetting& config,
    McpOAuthStoredRefresh prepared,McpDeadline deadline,std::stop_token cancel){
    if(!prepared.grant)return prepared.record;
    const auto id=prepared.record.binding.request_id,generation=prepared.record.generation;
    McpOAuthCredentialStore grants(store);
    try{
        if(prepared.record.state!="prepared"||!config.oauth||prepared.record.binding.server_json!=mcp_server_setting_json(config))throw Conflict("MCP refresh preparation binding differs");
        const auto check=[&]{if(cancel.stop_requested())throw McpTransportCancelled("MCP refresh cancelled");if(std::chrono::steady_clock::now()>=deadline)throw McpTransportTimeout("MCP refresh deadline exceeded");};check();
        const auto network_deadline=[&]{return std::min(deadline,std::chrono::steady_clock::now()+std::chrono::seconds(30));};
        std::optional<McpOAuthChallenge> challenge;
        try{McpHttpClient probe(config.endpoint);probe.connect(network_deadline(),cancel);probe.shutdown();}
        catch(const McpOAuthAuthorizationRequired& required){challenge=required.challenge;}
        auto discovery=discover_mcp_oauth(config.endpoint,challenge?challenge->metadata_url:std::nullopt,network_deadline(),cancel,config.oauth->issuer);check();
        if(!prepared.grant->tokens.refresh_token)throw McpProtocolError("MCP refresh token is absent");
        McpOAuthRefreshAttempt attempt(std::move(discovery),config.oauth->client_id,prepared.grant->token_endpoint,
            std::move(*prepared.grant->tokens.refresh_token),prepared.grant->tokens.scopes,deadline);
        check();const auto dispatched=grants.dispatch_refresh(prepared.record);
        auto tokens=attempt.exchange(network_deadline(),cancel);check();
        const auto acquired=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        return grants.publish_refresh(config,dispatched,std::move(tokens),acquired);
    }catch(...){
        // Inspect first: a lost publication acknowledgement can follow an actual
        // atomic commit. Never overwrite its receipt or issue another exchange.
        auto observed=store.mcp_oauth_refresh(id).get();
        if(observed.state=="committed"||observed.state=="cancelled"||observed.state=="uncertain")return observed;
        if(observed.generation!=generation)throw Conflict("MCP refresh owner generation changed");
        try{return grants.abandon_refresh(observed);}catch(...){
            observed=store.mcp_oauth_refresh(id).get();
            if(observed.state=="committed"||observed.state=="cancelled"||observed.state=="uncertain")return observed;
            throw Conflict("MCP refresh requires durable ownership recovery");
        }
    }
}
}
