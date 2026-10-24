#include "agentflow/remote_negotiation.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <set>

namespace agentflow {
namespace {
using Json=nlohmann::json;
[[noreturn]]void fail(RemoteNegotiationFailure reason=RemoteNegotiationFailure::malformed){throw RemoteNegotiationError(reason);}
bool identifier(const std::string& value){return !value.empty()&&value.size()<=256&&std::all_of(value.begin(),value.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c==':';});}
std::string id(const Json& value){if(!value.is_string())fail();auto result=value.get<std::string>();if(!identifier(result))fail();return result;}
std::int64_t positive(const Json& value){if(!value.is_number_integer()||value.is_boolean())fail();if(value.is_number_unsigned()&&value.get<std::uint64_t>()>INT64_MAX)fail();const auto result=value.get<std::int64_t>();if(result<=0)fail();return result;}
void keys(const Json& value,std::initializer_list<const char*> fields){if(!value.is_object()||value.size()!=fields.size())fail();for(const auto* field:fields)if(!value.contains(field))fail();}
void requirements_valid(const RemoteNegotiationRequirements& r){
    if(r.now_unix_seconds<=0||r.supported_versions.empty()||r.supported_versions.size()>16||r.required_capabilities.size()>128||(r.expected_server_id&&!identifier(*r.expected_server_id)))throw std::invalid_argument("Invalid remote negotiation requirements");
    std::set<std::int64_t> versions;for(const auto v:r.supported_versions)if(v<=0||!versions.insert(v).second)throw std::invalid_argument("Invalid remote negotiation requirements");
    for(const auto& [name,version]:r.required_capabilities)if(!identifier(name)||version<=0)throw std::invalid_argument("Invalid remote negotiation requirements");
}
}
RemoteNegotiationError::RemoteNegotiationError(RemoteNegotiationFailure reason):std::runtime_error(
    reason==RemoteNegotiationFailure::server_mismatch?"Remote server identity mismatch":
    reason==RemoteNegotiationFailure::unsupported_version?"Remote protocol version unsupported":
    reason==RemoteNegotiationFailure::missing_capability?"Required remote capability unavailable":
    reason==RemoteNegotiationFailure::expired_grant?"Remote grant expired":"Malformed remote negotiation response"),reason_(reason){}
std::string encode_remote_negotiation_request(const RemoteNegotiationRequirements& requirements){requirements_valid(requirements);return Json{{"supported_versions",requirements.supported_versions},{"required_capabilities",requirements.required_capabilities},{"expected_server_id",requirements.expected_server_id?Json(*requirements.expected_server_id):Json(nullptr)}}.dump();}
RemoteNegotiatedGrant validate_remote_negotiation(std::string_view response,const RemoteNegotiationRequirements& requirements){
    requirements_valid(requirements);if(response.empty()||response.size()>65536)fail();
    try{
        std::vector<std::set<std::string>> objects;
        const auto body=Json::parse(response,[&](int depth,Json::parse_event_t event,Json& parsed){
            if(depth>8)fail();
            if(event==Json::parse_event_t::object_start)objects.emplace_back();
            else if(event==Json::parse_event_t::key){if(objects.empty()||!objects.back().insert(parsed.get<std::string>()).second)fail();}
            else if(event==Json::parse_event_t::object_end){if(objects.empty())fail();objects.pop_back();}
            return true;
        });
        keys(body,{"server_id","protocol_version","grant_id","grant_revision","expires_at","authorized_scopes","capabilities"});RemoteNegotiatedGrant grant;
        grant.server_id=id(body.at("server_id"));grant.grant_id=id(body.at("grant_id"));grant.protocol_version=positive(body.at("protocol_version"));grant.grant_revision=positive(body.at("grant_revision"));grant.expires_at=positive(body.at("expires_at"));
        if(requirements.expected_server_id&&*requirements.expected_server_id!=grant.server_id)fail(RemoteNegotiationFailure::server_mismatch);
        if(grant.protocol_version!=1||std::find(requirements.supported_versions.begin(),requirements.supported_versions.end(),grant.protocol_version)==requirements.supported_versions.end())fail(RemoteNegotiationFailure::unsupported_version);
        if(grant.expires_at<=requirements.now_unix_seconds)fail(RemoteNegotiationFailure::expired_grant);
        const auto& caps=body.at("capabilities");if(!caps.is_object()||caps.size()>128)fail();for(const auto& [name,value]:caps.items()){if(!identifier(name))fail();grant.capabilities.emplace(name,positive(value));}
        for(const auto& [name,version]:requirements.required_capabilities){const auto it=grant.capabilities.find(name);if(it==grant.capabilities.end()||it->second!=version)fail(RemoteNegotiationFailure::missing_capability);}
        const auto& scopes=body.at("authorized_scopes");if(!scopes.is_array()||scopes.empty()||scopes.size()>128)fail();std::set<std::string> scope_ids;
        for(const auto& scope:scopes){keys(scope,{"id","actions"});RemoteAuthorizedScope result;result.id=id(scope.at("id"));if(!scope_ids.insert(result.id).second)fail();const auto& actions=scope.at("actions");if(!actions.is_array()||actions.empty()||actions.size()>64)fail();std::set<std::string> seen;for(const auto& action:actions){auto name=id(action);if(!seen.insert(name).second)fail();result.actions.push_back(std::move(name));}grant.authorized_scopes.push_back(std::move(result));}
        return grant;
    }catch(const Json::exception&){fail();}
}
}
