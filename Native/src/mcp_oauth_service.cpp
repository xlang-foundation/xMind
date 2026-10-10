#include "agentflow/mcp_oauth_service.hpp"
#include "agentflow/mcp_oauth_credentials.hpp"
#include "agentflow/mcp_oauth_callback.hpp"
#include "agentflow/mcp_http_client.hpp"
#include "agentflow/mcp_wire.hpp"
#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace agentflow {
namespace {
std::int64_t wall(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
bool terminal(const std::string& state){return state=="connected"||state=="failed"||state=="denied"||state=="cancelled"||state=="expired";}
void identity(const std::string& value){if(value.empty()||value.size()>128||value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::invalid_argument("Invalid MCP authorization request identity");}
}
struct McpOAuthService::Impl {
    struct Entry {
        McpServerSetting config;
        std::int64_t expected_credential_revision;
        McpDeadline deadline;
        McpOAuthAttemptStatus value;
        std::stop_source cancellation;
        std::jthread worker;
    };
    PersistenceService& store;
    std::vector<McpServerSetting> settings;
    std::mutex mutex,stop_mutex;
    std::map<std::string,std::shared_ptr<Entry>> entries;
    std::set<std::string> retired;
    bool stopping=false;
    Impl(PersistenceService& persistence,std::vector<McpServerSetting> configured):store(persistence),settings(std::move(configured)){
        if(settings.size()>16)throw std::invalid_argument("MCP authorization server count exceeds limits");
        std::set<std::string> ids;for(const auto& config:settings){if(!ids.insert(config.id).second||config.revision<1)throw std::invalid_argument("Invalid MCP authorization configuration");if(config.oauth)(void)mcp_credential_purpose(config,"OAUTH");}
    }
    McpOAuthServerStatus observe(const McpServerSetting& config){
        McpOAuthServerStatus value{config.id,config.enabled?"not_configured":"disabled",config.revision,0,config.enabled,bool(config.oauth),{}};
        if(!config.oauth)return value;
        try{auto grant=McpOAuthCredentialStore(store).load(config);value.credential_revision=grant.revision;value.expires_unix_ms=grant.expires_unix_ms;if(config.enabled)value.state=grant.usable_at(wall())?"authorized":"needs_login";}
        catch(const NotFound&){if(config.enabled)value.state="needs_login";}
        catch(const PersistenceBusy&){throw;}
        catch(const PersistenceClosed&){throw;}
        catch(...){value.state="unavailable";}
        return value;
    }
    void phase(const std::shared_ptr<Entry>& entry,std::string state,std::string url={}){
        std::lock_guard lock(mutex);
        if(entry->cancellation.stop_requested())throw McpTransportCancelled("MCP authorization cancelled");
        entry->value.state=std::move(state);entry->value.authorization_url=std::move(url);
    }
    void finish(const std::shared_ptr<Entry>& entry,std::string state,std::string reason={},std::int64_t revision=0){
        std::lock_guard lock(mutex);
        // Cancellation cannot turn an already committed encrypted grant into
        // an uncommitted result. Commit and final publication share this lock.
        entry->value.state=std::move(state);entry->value.reason=std::move(reason);entry->value.authorization_url.clear();
        if(revision)entry->value.credential_revision=revision;
    }
    void run(const std::shared_ptr<Entry>& entry){
        const auto stop=entry->cancellation.get_token();
        try{
            const auto network_deadline=[&]{return std::min(entry->deadline,std::chrono::steady_clock::now()+std::chrono::seconds(30));};
            std::optional<McpOAuthChallenge> challenge;
            try{McpHttpClient probe(entry->config.endpoint);probe.connect(network_deadline(),stop);probe.shutdown();}
            catch(const McpOAuthAuthorizationRequired& required){challenge=required.challenge;}
            auto discovery=discover_mcp_oauth(entry->config.endpoint,challenge?challenge->metadata_url:std::nullopt,network_deadline(),stop,entry->config.oauth->issuer);
            const auto scopes=challenge&&!challenge->scopes.empty()?challenge->scopes:discovery.resource.scopes;
            McpOAuthLoopbackCallback callback(entry->config.oauth->callback.path,entry->config.oauth->callback.port);
            McpOAuthAuthorizationAttempt attempt(discovery,{entry->config.oauth->issuer,entry->config.oauth->client_id,callback.redirect_uri()},scopes,entry->deadline);
            phase(entry,"awaiting_callback",attempt.authorization_url());
            callback.receive(attempt,entry->deadline,stop);
            phase(entry,"exchanging");
            auto tokens=attempt.exchange(network_deadline(),stop);
            // Credential publication and explicit cancellation are serialized.
            // SQLite mutation additionally fences quiescence/owner generations.
            std::lock_guard lock(mutex);
            if(stop.stop_requested())throw McpTransportCancelled("MCP authorization cancelled");
            const auto current=McpConfigurationStore(store).load();
            const auto found=std::find_if(current.begin(),current.end(),[&](const auto& item){return item.id==entry->config.id;});
            if(found==current.end()||!found->enabled||found->revision!=entry->config.revision||mcp_credential_purpose(*found,"OAUTH")!=mcp_credential_purpose(entry->config,"OAUTH")||found->oauth->id!=entry->config.oauth->id)throw Conflict("MCP authorization configuration changed");
            const auto saved=McpOAuthCredentialStore(store).save(entry->config,discovery.authorization.token_endpoint,std::move(tokens),wall(),entry->expected_credential_revision);
            entry->value.state="connected";entry->value.credential_revision=saved.revision;entry->value.authorization_url.clear();
        }catch(const McpOAuthAuthorizationDenied& error){finish(entry,"denied",error.reason);}
        catch(const McpTransportCancelled&){finish(entry,"cancelled","cancelled");}
        catch(const McpTransportTimeout&){finish(entry,"expired","deadline_exceeded");}
        catch(const BackendQuiesced&){finish(entry,"failed","backend_quiesced");}
        catch(const Conflict&){finish(entry,"failed","configuration_or_credential_changed");}
        catch(const McpProtocolError&){finish(entry,"failed","protocol_rejected");}
        catch(...){finish(entry,stop.stop_requested()?"cancelled":"failed",stop.stop_requested()?"cancelled":"authorization_failed");}
    }
};
McpOAuthService::McpOAuthService(PersistenceService& store,std::vector<McpServerSetting> settings):impl_(std::make_unique<Impl>(store,std::move(settings))){}
McpOAuthService::~McpOAuthService(){stop();}
std::vector<McpOAuthServerStatus> McpOAuthService::servers(){std::vector<McpOAuthServerStatus> result;for(const auto& config:impl_->settings)result.push_back(impl_->observe(config));return result;}
McpOAuthAttemptStatus McpOAuthService::start(std::string server_id,std::int64_t revision,std::int64_t credential_revision,std::string id){
    identity(id);if(revision<1||credential_revision<0||credential_revision>9007199254740991LL)throw std::invalid_argument("Invalid MCP authorization revision");
    auto& owner=*impl_;std::lock_guard lock(owner.mutex);if(owner.stopping)throw PersistenceClosed("MCP authorization service is stopping");
    if(const auto found=owner.entries.find(id);found!=owner.entries.end()){
        if(found->second->config.id!=server_id||found->second->config.revision!=revision||found->second->expected_credential_revision!=credential_revision)throw Conflict("MCP authorization request identity changed");return found->second->value;
    }
    if(owner.retired.contains(id))throw Conflict("MCP authorization request identity was retired");
    const auto config=std::find_if(owner.settings.begin(),owner.settings.end(),[&](const auto& item){return item.id==server_id;});if(config==owner.settings.end())throw NotFound("MCP server not found");
    if(!config->enabled||!config->oauth||config->revision!=revision)throw Conflict("MCP authorization configuration is unavailable or changed");
    const auto observed=owner.observe(*config);if(observed.state=="unavailable"||observed.credential_revision!=credential_revision)throw Conflict("MCP authorization credential changed or is unavailable");
    if(observed.state=="authorized")throw Conflict("MCP authorization is already usable");
    std::size_t active=0;for(const auto& [key,value]:owner.entries){(void)key;if(!terminal(value->value.state)){++active;if(value->config.id==server_id)throw Conflict("MCP authorization is already pending");}}
    if(active>=4)throw PersistenceBusy("MCP authorization capacity is full");
    if(owner.entries.size()>=64){const auto old=std::find_if(owner.entries.begin(),owner.entries.end(),[](const auto& item){return terminal(item.second->value.state);});if(old==owner.entries.end()||owner.retired.size()>=4096)throw PersistenceBusy("MCP authorization history capacity is full");if(old->second->worker.joinable())old->second->worker.join();owner.retired.insert(old->first);owner.entries.erase(old);}
    auto entry=std::make_shared<Impl::Entry>();entry->config=*config;entry->expected_credential_revision=credential_revision;entry->deadline=std::chrono::steady_clock::now()+std::chrono::minutes(5);
    entry->value={id,server_id,"discovering","","",revision,credential_revision,wall()+300000,false};owner.entries.emplace(id,entry);
    try{entry->worker=std::jthread([&owner,entry]{owner.run(entry);});}catch(...){owner.entries.erase(id);throw;}
    return entry->value;
}
McpOAuthAttemptStatus McpOAuthService::status(const std::string& id){identity(id);std::lock_guard lock(impl_->mutex);const auto found=impl_->entries.find(id);if(found==impl_->entries.end())throw NotFound("MCP authorization attempt not found");return found->second->value;}
McpOAuthAttemptStatus McpOAuthService::cancel(const std::string& id){identity(id);std::lock_guard lock(impl_->mutex);const auto found=impl_->entries.find(id);if(found==impl_->entries.end())throw NotFound("MCP authorization attempt not found");auto& entry=*found->second;if(!terminal(entry.value.state)){entry.value.cancellation_requested=true;entry.cancellation.request_stop();}return entry.value;}
void McpOAuthService::stop(){auto& owner=*impl_;std::lock_guard stopping(owner.stop_mutex);std::vector<std::shared_ptr<Impl::Entry>> entries;{std::lock_guard lock(owner.mutex);owner.stopping=true;for(const auto& [id,entry]:owner.entries){(void)id;entry->cancellation.request_stop();entries.push_back(entry);}}for(const auto& entry:entries)if(entry->worker.joinable())entry->worker.join();}
}
