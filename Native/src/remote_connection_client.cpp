#include "agentflow/remote_connection_client.hpp"
#include "agentflow/connection_profiles.hpp"
#include "agentflow/http_stream_transport.hpp"
#include "agentflow/context_records.hpp"
#include <charconv>
#include <algorithm>
#include <set>
#include "nlohmann/json.hpp"

namespace agentflow {
namespace {
void endpoint_origin(const std::string& value,RemoteTransportPolicy policy){
    if(value.starts_with("https://")){validate_remote_connection_origin(value);return;}
    constexpr std::string_view prefix="http://127.0.0.1:";
    if(policy!=RemoteTransportPolicy::isolated_loopback_test||!value.starts_with(prefix))throw std::invalid_argument("Remote negotiation requires verified HTTPS");
    const auto port=std::string_view(value).substr(prefix.size());unsigned parsed=0;const auto result=std::from_chars(port.data(),port.data()+port.size(),parsed);
    if(result.ec!=std::errc{}||result.ptr!=port.data()+port.size()||parsed==0||parsed>65535)throw std::invalid_argument("Invalid isolated remote test origin");
}
std::int64_t now(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
bool identifier(const std::string& value){return !value.empty()&&value.size()<=256&&value!="."&&value!=".."&&std::all_of(value.begin(),value.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c==':';});}
std::string component(const std::string& value){if(!identifier(value))throw std::invalid_argument("Invalid remote resource identity");std::string result;for(const auto c:value)if(c==':')result+="%3A";else result+=c;return result;}
[[noreturn]]void invalid_session(){throw TransportError("Invalid remote session observation");}
void keys(const nlohmann::json& value,std::initializer_list<const char*> names){if(!value.is_object()||value.size()!=names.size())invalid_session();for(const auto* name:names)if(!value.contains(name))invalid_session();}
}
void validate_remote_session_scope(const RemoteNegotiatedGrant& grant,const std::string& scope,std::int64_t at){
    if(!identifier(scope)||at<=0)throw std::invalid_argument("Invalid remote scope identity");RemoteNegotiationRequirements requirements;requirements.now_unix_seconds=at;requirements.expected_server_id=grant.server_id;requirements.required_capabilities={{"sessions",1}};const auto checked=validate_remote_negotiation(encode_remote_negotiated_grant(grant),requirements);
    const auto found=std::find_if(checked.authorized_scopes.begin(),checked.authorized_scopes.end(),[&](const auto& item){return item.id==scope;});if(found==checked.authorized_scopes.end()||std::find(found->actions.begin(),found->actions.end(),"sessions.read")==found->actions.end())throw TransportError("Remote scope does not authorize session observation");
}
RemoteObservedSession read_remote_session(const std::string& origin,const SecretBytes& credential,const RemoteNegotiatedGrant& grant,const std::string& scope,const std::string& session,std::chrono::milliseconds timeout,RemoteTransportPolicy policy,std::stop_token stop){
    endpoint_origin(origin,policy);validate_remote_session_scope(grant,scope,now());const auto resource=component(session);
    if(timeout.count()<=0||timeout>std::chrono::minutes(2)||credential.view().empty()||credential.view().size()>8192)throw std::invalid_argument("Invalid remote session request");for(const auto c:credential.view())if(c<33||c>126)throw std::invalid_argument("Invalid remote credential");
    HttpStreamRequest request;request.url=origin+"/xmind/v1/scopes/"+component(scope)+"/sessions/"+resource;request.deadline=timeout;request.idle_timeout=timeout;request.remote_binding=RemoteHttpBinding{grant.server_id,scope,grant.grant_id,1,grant.grant_revision};
    const auto raw=get_bounded_json(request,&credential,65536,stop);validate_remote_session_scope(grant,scope,now());if(stop.stop_requested())throw TransportCancelled("Remote session observation cancelled");
    try{using Json=nlohmann::json;std::vector<std::set<std::string>> objects;const auto response=Json::parse(raw,[&](int depth,Json::parse_event_t event,Json& value){if(depth>4)invalid_session();if(event==Json::parse_event_t::object_start)objects.emplace_back();else if(event==Json::parse_event_t::key){if(objects.empty()||!objects.back().insert(value.get<std::string>()).second)invalid_session();}else if(event==Json::parse_event_t::object_end)objects.pop_back();return true;});
        keys(response,{"binding","session"});const auto& binding=response.at("binding");keys(binding,{"server_id","scope_id","protocol_version","grant_id","grant_revision"});
        if(!binding.at("server_id").is_string()||binding.at("server_id")!=grant.server_id||!binding.at("scope_id").is_string()||binding.at("scope_id")!=scope||!binding.at("grant_id").is_string()||binding.at("grant_id")!=grant.grant_id||!binding.at("protocol_version").is_number_integer()||binding.at("protocol_version")!=1||!binding.at("grant_revision").is_number_integer()||binding.at("grant_revision")!=grant.grant_revision)invalid_session();
        const auto& value=response.at("session");keys(value,{"id","title","revision"});if(!value.at("id").is_string()||value.at("id")!=session||!value.at("title").is_string()||value.at("title").get_ref<const std::string&>().size()>4096||!value.at("revision").is_number_integer()||value.at("revision")<1||value.at("revision")>9007199254740991LL)invalid_session();return {value.at("id"),value.at("title"),value.at("revision")};
    }catch(const nlohmann::json::exception&){invalid_session();}
}
void validate_remote_run_scope(const RemoteNegotiatedGrant& grant,const std::string& scope,std::int64_t at){
    if(!identifier(scope)||at<=0)throw std::invalid_argument("Invalid remote scope identity");
    RemoteNegotiationRequirements requirements;requirements.now_unix_seconds=at;requirements.expected_server_id=grant.server_id;requirements.required_capabilities={{"runs",1}};
    const auto checked=validate_remote_negotiation(encode_remote_negotiated_grant(grant),requirements);
    const auto found=std::find_if(checked.authorized_scopes.begin(),checked.authorized_scopes.end(),[&](const auto& item){return item.id==scope;});
    if(found==checked.authorized_scopes.end()||std::find(found->actions.begin(),found->actions.end(),"runs.read")==found->actions.end())throw TransportError("Remote scope does not authorize run observation");
}
RemoteObservedRun read_remote_run(const std::string& origin,const SecretBytes& credential,const RemoteNegotiatedGrant& grant,const std::string& scope,const std::string& session,const std::string& run,std::chrono::milliseconds timeout,RemoteTransportPolicy policy,std::stop_token stop){
    endpoint_origin(origin,policy);validate_remote_run_scope(grant,scope,now());const auto session_resource=component(session),run_resource=component(run);
    if(timeout.count()<=0||timeout>std::chrono::minutes(2)||credential.view().empty()||credential.view().size()>8192)throw std::invalid_argument("Invalid remote run request");
    for(const auto c:credential.view())if(c<33||c>126)throw std::invalid_argument("Invalid remote credential");
    HttpStreamRequest request;request.url=origin+"/xmind/v1/scopes/"+component(scope)+"/sessions/"+session_resource+"/runs/"+run_resource;request.deadline=timeout;request.idle_timeout=timeout;
    request.remote_binding=RemoteHttpBinding{grant.server_id,scope,grant.grant_id,1,grant.grant_revision};
    const auto raw=get_bounded_json(request,&credential,65536,stop);validate_remote_run_scope(grant,scope,now());if(stop.stop_requested())throw TransportCancelled("Remote run observation cancelled");
    try{using Json=nlohmann::json;std::vector<std::set<std::string>> objects;const auto response=Json::parse(raw,[&](int depth,Json::parse_event_t event,Json& value){if(depth>4)throw TransportError("Invalid remote run observation");if(event==Json::parse_event_t::object_start)objects.emplace_back();else if(event==Json::parse_event_t::key){if(objects.empty()||!objects.back().insert(value.get<std::string>()).second)throw TransportError("Invalid remote run observation");}else if(event==Json::parse_event_t::object_end)objects.pop_back();return true;});
        keys(response,{"binding","run"});const auto& binding=response.at("binding");keys(binding,{"server_id","scope_id","protocol_version","grant_id","grant_revision"});
        if(binding.at("server_id")!=grant.server_id||binding.at("scope_id")!=scope||binding.at("grant_id")!=grant.grant_id||!binding.at("protocol_version").is_number_integer()||binding.at("protocol_version")!=1||!binding.at("grant_revision").is_number_integer()||binding.at("grant_revision")!=grant.grant_revision)throw TransportError("Invalid remote run binding");
        const auto& value=response.at("run");keys(value,{"id","session_id","root_id","state","revision"});
        if(value.at("id")!=run||value.at("session_id")!=session||!value.at("root_id").is_string()||!identifier(value.at("root_id").get_ref<const std::string&>())||!value.at("state").is_string()||!value.at("revision").is_number_integer()||value.at("revision")<1||value.at("revision")>9007199254740991LL)throw TransportError("Invalid remote run observation");
        const auto state=value.at("state").get<std::string>();if(state!="queued"&&state!="running"&&state!="paused"&&state!="completed"&&state!="failed"&&state!="cancelled")throw TransportError("Invalid remote run state");
        return {value.at("id"),value.at("session_id"),value.at("root_id"),state,value.at("revision")};
    }catch(const nlohmann::json::exception&){throw TransportError("Invalid remote run observation");}
}
namespace {
void command_scope(const RemoteNegotiatedGrant& grant,const std::string& scope,std::int64_t at,bool create,bool run=false){
 if(!identifier(scope)||at<=0)throw std::invalid_argument("Invalid remote scope identity");RemoteNegotiationRequirements needs;needs.now_unix_seconds=at;needs.expected_server_id=grant.server_id;needs.required_capabilities={{run?"runs":"sessions",1},{"receipts",1}};const auto checked=validate_remote_negotiation(encode_remote_negotiated_grant(grant),needs);const auto found=std::find_if(checked.authorized_scopes.begin(),checked.authorized_scopes.end(),[&](const auto& s){return s.id==scope;});const auto action=create?(run?"runs.submit":"sessions.create"):"receipts.read";if(found==checked.authorized_scopes.end()||std::find(found->actions.begin(),found->actions.end(),action)==found->actions.end())throw TransportError("Remote scope does not authorize command request");
}
RemoteCommandObservation command_request(const std::string& origin,const SecretBytes& credential,const RemoteNegotiatedGrant& grant,const std::string& scope,const std::string& id,const RemoteCommandAction& action,std::chrono::milliseconds timeout,RemoteTransportPolicy policy,std::stop_token stop,bool create){
 const auto* run=std::get_if<RemoteRunSubmitAction>(&action);const auto* session=std::get_if<RemoteSessionCreateAction>(&action);if(!run&&!session)throw std::invalid_argument("Invalid remote command action");
 endpoint_origin(origin,policy);command_scope(grant,scope,now(),create,run!=nullptr);if(create)command_scope(grant,scope,now(),false,run!=nullptr);const auto resource=component(id);
 if((session&&session->title.size()>4096)||(run&&(!identifier(run->session_id)||run->prompt.empty()||run->prompt.size()>262144||run->expected_session_revision<1||run->expected_session_revision>9007199254740991LL))||timeout.count()<=0||timeout>std::chrono::minutes(2)||credential.view().empty()||credential.view().size()>8192)throw std::invalid_argument("Invalid remote command request");for(const auto c:credential.view())if(c<33||c>126)throw std::invalid_argument("Invalid remote credential");
 using Json=nlohmann::json;const auto operation=run?"runs.submit":"sessions.create";HttpStreamRequest request;request.url=origin+"/xmind/v1/scopes/"+component(scope)+(create?(run?"/sessions/"+component(run->session_id)+"/runs":std::string("/sessions")):"/requests/"+resource);request.deadline=timeout;request.idle_timeout=timeout;request.remote_binding=RemoteHttpBinding{grant.server_id,scope,grant.grant_id,1,grant.grant_revision};
 try{if(create){if(run)request.body=Json{{"request_id",id},{"operation",operation},{"session_id",run->session_id},{"expected_revision",run->expected_session_revision},{"prompt",run->prompt}}.dump();else request.body=Json{{"request_id",id},{"operation",operation},{"expected_revision",0},{"title",session->title}}.dump();}else (void)Json(run?run->prompt:session->title).dump();}catch(const Json::exception&){throw std::invalid_argument("Invalid remote command text encoding");}
 const auto prompt_digest=run?context_digest(run->prompt):std::string{};
 const auto raw=create?post_json(request,&credential,65536,stop):get_bounded_json(request,&credential,65536,stop);command_scope(grant,scope,now(),create,run!=nullptr);if(stop.stop_requested())throw TransportCancelled("Remote command observation cancelled");
 try{std::vector<std::set<std::string>> objects;const auto value=Json::parse(raw,[&](int depth,Json::parse_event_t event,Json& v){if(depth>4)invalid_session();if(event==Json::parse_event_t::object_start)objects.emplace_back();else if(event==Json::parse_event_t::key){if(objects.empty()||!objects.back().insert(v.get<std::string>()).second)invalid_session();}else if(event==Json::parse_event_t::object_end)objects.pop_back();return true;});keys(value,{"binding","receipt"});const auto& b=value.at("binding");keys(b,{"server_id","scope_id","protocol_version","grant_id","grant_revision"});if(b.at("server_id")!=grant.server_id||b.at("scope_id")!=scope||b.at("grant_id")!=grant.grant_id||!b.at("protocol_version").is_number_integer()||b.at("protocol_version")!=1||!b.at("grant_revision").is_number_integer()||b.at("grant_revision")!=grant.grant_revision)invalid_session();
 const auto& r=value.at("receipt");if(r.is_object()&&r.contains("state")&&r.at("state")=="not_found"){if(create)invalid_session();keys(r,{"request_id","state"});if(r.at("request_id")!=id)invalid_session();return {"not_found",std::nullopt};}
 if(run){keys(r,{"request_id","operation","session_id","expected_revision","prompt_sha256","state","resource_id","resource_revision"});if(r.at("session_id")!=run->session_id||r.at("prompt_sha256")!=prompt_digest)invalid_session();}else{keys(r,{"request_id","operation","expected_revision","title","state","resource_id","resource_revision"});if(r.at("title")!=session->title)invalid_session();}
 if(r.at("request_id")!=id||r.at("operation")!=operation||!r.at("expected_revision").is_number_integer()||r.at("expected_revision")!=(run?run->expected_session_revision:0)||!r.at("state").is_string()||!r.at("resource_id").is_string()||!r.at("resource_revision").is_number_integer())invalid_session();const auto state=r.at("state").get<std::string>();
 if(state=="admitted"){const auto target=r.at("resource_id").get<std::string>();if(!identifier(target)||r.at("resource_revision")<1||r.at("resource_revision")>9007199254740991LL)invalid_session();return {state,RemoteCommandResolution{id,grant.server_id,scope,operation,target,RemoteCommandState::admitted,r.at("resource_revision")}};}
 if(r.at("resource_id")!=""||r.at("resource_revision")!=0)invalid_session();if(state=="rejected")return {state,RemoteCommandResolution{id,grant.server_id,scope,operation,"",RemoteCommandState::rejected,0}};if(state=="pending")return {state,std::nullopt};invalid_session();
 }catch(const Json::exception&){throw TransportError("Invalid remote command observation");}
}
}
void validate_remote_receipt_scope(const RemoteNegotiatedGrant& g,const std::string& s,std::int64_t at){command_scope(g,s,at,false);}
void validate_remote_session_create_scope(const RemoteNegotiatedGrant& g,const std::string& s,std::int64_t at){command_scope(g,s,at,true);command_scope(g,s,at,false);}
void validate_remote_run_submit_scope(const RemoteNegotiatedGrant& g,const std::string& s,std::int64_t at){command_scope(g,s,at,true,true);command_scope(g,s,at,false,true);}
void validate_remote_run_receipt_scope(const RemoteNegotiatedGrant& g,const std::string& s,std::int64_t at){command_scope(g,s,at,false,true);}
RemoteCommandObservation create_remote_session(const std::string& o,const SecretBytes& c,const RemoteNegotiatedGrant& g,const std::string& s,const std::string& id,const std::string& t,std::chrono::milliseconds timeout,RemoteTransportPolicy p,std::stop_token stop){return command_request(o,c,g,s,id,RemoteSessionCreateAction{t},timeout,p,stop,true);}
RemoteCommandObservation observe_remote_session_request(const std::string& o,const SecretBytes& c,const RemoteNegotiatedGrant& g,const std::string& s,const std::string& id,const std::string& t,std::chrono::milliseconds timeout,RemoteTransportPolicy p,std::stop_token stop){return command_request(o,c,g,s,id,RemoteSessionCreateAction{t},timeout,p,stop,false);}
RemoteCommandObservation submit_remote_run(const std::string& o,const SecretBytes& c,const RemoteNegotiatedGrant& g,const std::string& s,const std::string& id,const RemoteRunSubmitAction& a,std::chrono::milliseconds timeout,RemoteTransportPolicy p,std::stop_token stop){return command_request(o,c,g,s,id,a,timeout,p,stop,true);}
RemoteCommandObservation observe_remote_run_request(const std::string& o,const SecretBytes& c,const RemoteNegotiatedGrant& g,const std::string& s,const std::string& id,const RemoteRunSubmitAction& a,std::chrono::milliseconds timeout,RemoteTransportPolicy p,std::stop_token stop){return command_request(o,c,g,s,id,a,timeout,p,stop,false);}
RemoteNegotiatedGrant negotiate_remote_connection(const std::string& origin,const SecretBytes& credential,RemoteNegotiationRequirements requirements,std::chrono::milliseconds timeout,RemoteTransportPolicy policy,std::stop_token stop){
    endpoint_origin(origin,policy);
    if(timeout.count()<=0||timeout>std::chrono::minutes(2))throw std::invalid_argument("Invalid remote negotiation timeout");
    if(credential.view().empty()||credential.view().size()>8192)throw std::invalid_argument("Invalid remote credential");
    for(const auto byte:credential.view())if(byte<33||byte>126)throw std::invalid_argument("Invalid remote credential");
    if(requirements.supported_versions!=std::vector<std::int64_t>{1})throw std::invalid_argument("Remote connector supports only protocol v1");
    requirements.now_unix_seconds=now();HttpStreamRequest request;request.url=origin+"/xmind/v1/negotiate";request.body=encode_remote_negotiation_request(requirements);request.deadline=timeout;request.idle_timeout=timeout;
    if(stop.stop_requested())throw TransportCancelled("Remote negotiation cancelled before send");
    const auto response=post_json(request,&credential,65536,stop);
    requirements.now_unix_seconds=now();return validate_remote_negotiation(response,requirements);
}
}
