#include "agentflow/remote_run_snapshot.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <set>

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr std::int64_t maximum=9007199254740991LL;
[[noreturn]]void invalid(){throw TransportError("Invalid remote run snapshot page");}
bool identifier(const std::string& v){return !v.empty()&&v.size()<=256&&v!="."&&v!=".."&&v.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-:")==std::string::npos;}
void keys(const Json& v,std::initializer_list<const char*> names){if(!v.is_object()||v.size()!=names.size())invalid();for(const auto* n:names)if(!v.contains(n))invalid();}
std::int64_t integer(const Json& v,std::int64_t low,std::int64_t high=maximum){if(!v.is_number_integer()||v<low||v>high)invalid();return v.get<std::int64_t>();}
std::string id(const Json& v){if(!v.is_string()||!identifier(v.get_ref<const std::string&>()))invalid();return v.get<std::string>();}
void position(const RemoteSnapshotPosition& p){if(!identifier(p.snapshot_id)||!identifier(p.epoch)||p.sequence<0||p.sequence>maximum||p.total_records<1||p.total_records>4096||p.offset<1||p.offset>=p.total_records)throw std::invalid_argument("Invalid remote snapshot continuation");}
}
void validate_remote_run_snapshot_request(const RemoteNegotiatedGrant& grant,const RemoteRunEventCursor& resource,std::int64_t at,const std::optional<RemoteSnapshotPosition>& continuation){
    if(!identifier(resource.connection_id)||!identifier(resource.server_id)||!identifier(resource.scope_id)||!identifier(resource.session_id)||!identifier(resource.run_id)||resource.server_id!=grant.server_id||resource.sequence<0||resource.sequence>maximum||(!resource.epoch.empty()&&!identifier(resource.epoch))||(resource.epoch.empty()&&resource.sequence!=0)||at<=0)throw std::invalid_argument("Invalid remote snapshot resource");
    if(continuation)position(*continuation);
    RemoteNegotiationRequirements needs;needs.expected_server_id=resource.server_id;needs.now_unix_seconds=at;needs.required_capabilities={{"snapshots",1},{"runs",1},{"events",1}};
    const auto checked=validate_remote_negotiation(encode_remote_negotiated_grant(grant),needs);
    const auto scope=std::find_if(checked.authorized_scopes.begin(),checked.authorized_scopes.end(),[&](const auto& s){return s.id==resource.scope_id;});
    if(scope==checked.authorized_scopes.end())throw TransportError("Remote scope does not authorize snapshots");
    for(const auto* action:{"snapshots.read","runs.read","events.read"})if(std::find(scope->actions.begin(),scope->actions.end(),action)==scope->actions.end())throw TransportError("Remote scope does not authorize snapshots");
}
RemoteRunSnapshotPage validate_remote_run_snapshot_page(std::string_view raw,const RemoteNegotiatedGrant& grant,const RemoteRunEventCursor& resource,std::int64_t at,const std::optional<RemoteSnapshotPosition>& continuation){
    validate_remote_run_snapshot_request(grant,resource,at,continuation);
    if(raw.empty()||raw.size()>65536)invalid();
    try{
        std::vector<std::set<std::string>> objects;
        const auto response=Json::parse(raw,[&](int depth,Json::parse_event_t event,Json& v){if(depth>24)invalid();if(event==Json::parse_event_t::object_start)objects.emplace_back();else if(event==Json::parse_event_t::key){if(objects.empty()||!objects.back().insert(v.get<std::string>()).second)invalid();}else if(event==Json::parse_event_t::object_end)objects.pop_back();return true;});
        keys(response,{"binding","snapshot","records","complete"});const auto& binding=response.at("binding");keys(binding,{"server_id","scope_id","protocol_version","grant_id","grant_revision"});
        if(binding.at("server_id")!=grant.server_id||binding.at("scope_id")!=resource.scope_id||binding.at("grant_id")!=grant.grant_id||integer(binding.at("protocol_version"),1)!=1||integer(binding.at("grant_revision"),1)!=grant.grant_revision)invalid();
        const auto& snapshot=response.at("snapshot");keys(snapshot,{"id","server_id","scope_id","session_id","run_id","epoch","sequence","total_records","offset"});
        if(snapshot.at("server_id")!=resource.server_id||snapshot.at("scope_id")!=resource.scope_id||snapshot.at("session_id")!=resource.session_id||snapshot.at("run_id")!=resource.run_id)invalid();
        RemoteRunSnapshotPage result;result.cursor=resource;result.position={id(snapshot.at("id")),id(snapshot.at("epoch")),integer(snapshot.at("sequence"),0),integer(snapshot.at("total_records"),1,4096),integer(snapshot.at("offset"),0,4095)};
        auto& p=result.position;result.cursor.epoch=p.epoch;result.cursor.sequence=p.sequence;
        if(p.epoch==resource.epoch&&p.sequence<resource.sequence)invalid();
        if(continuation){const auto& c=*continuation;if(p.snapshot_id!=c.snapshot_id||p.epoch!=c.epoch||p.sequence!=c.sequence||p.total_records!=c.total_records||p.offset!=c.offset)invalid();}else if(p.offset!=0)invalid();
        const auto& records=response.at("records");if(!records.is_array()||records.empty()||records.size()>256||p.offset+records.size()>static_cast<std::size_t>(p.total_records)||!response.at("complete").is_boolean())invalid();
        result.complete=response.at("complete");if(result.complete!=(p.offset+records.size()==static_cast<std::size_t>(p.total_records)))invalid();
        std::set<std::pair<std::string,std::string>> unique;
        for(const auto& record:records){keys(record,{"ordinal","kind","id","revision","payload"});if(integer(record.at("ordinal"),0,4095)!=p.offset+static_cast<std::int64_t>(result.records.size())||!record.at("kind").is_string()||!record.at("payload").is_object())invalid();
            const auto kind=record.at("kind").get<std::string>(),identity=id(record.at("id"));const std::set<std::string> kinds{"run","message","operation","decision","worker","budget"};if(!kinds.contains(kind)||!unique.emplace(kind,identity).second)invalid();
            if((kind=="run")!=(p.offset+result.records.size()==0)|| (kind=="run"&&identity!=resource.run_id))invalid();
            result.records.push_back({kind,identity,record.at("payload").dump(),integer(record.at("revision"),1)});
        }
        return result;
    }catch(const Json::exception&){invalid();}
}
void validate_remote_run_snapshot_result(const RemoteRunSnapshotPage& page,const RemoteNegotiatedGrant& grant,const RemoteRunEventCursor& resource,std::int64_t at,const std::optional<RemoteSnapshotPosition>& continuation){
    if(page.cursor.connection_id!=resource.connection_id||page.cursor.epoch!=page.position.epoch||page.cursor.sequence!=page.position.sequence||page.records.size()>256)invalid();
    try{
        Json records=Json::array();std::int64_t ordinal=page.position.offset;std::size_t bytes=0;
        for(const auto& record:page.records){bytes+=record.payload_json.size()+record.kind.size()+record.id.size();if(bytes>65536)invalid();std::vector<std::set<std::string>> objects;
            auto payload=Json::parse(record.payload_json,[&](int depth,Json::parse_event_t event,Json& v){if(depth>24)invalid();if(event==Json::parse_event_t::object_start)objects.emplace_back();else if(event==Json::parse_event_t::key){if(objects.empty()||!objects.back().insert(v.get<std::string>()).second)invalid();}else if(event==Json::parse_event_t::object_end)objects.pop_back();return true;});
            records.push_back({{"ordinal",ordinal++},{"kind",record.kind},{"id",record.id},{"revision",record.revision},{"payload",std::move(payload)}});}
        const Json wire={{"binding",{{"server_id",grant.server_id},{"scope_id",resource.scope_id},{"protocol_version",grant.protocol_version},{"grant_id",grant.grant_id},{"grant_revision",grant.grant_revision}}},{"snapshot",{{"id",page.position.snapshot_id},{"server_id",page.cursor.server_id},{"scope_id",page.cursor.scope_id},{"session_id",page.cursor.session_id},{"run_id",page.cursor.run_id},{"epoch",page.position.epoch},{"sequence",page.position.sequence},{"total_records",page.position.total_records},{"offset",page.position.offset}}},{"records",std::move(records)},{"complete",page.complete}};
        (void)validate_remote_run_snapshot_page(wire.dump(),grant,resource,at,continuation);
    }catch(const Json::exception&){invalid();}
}
}
