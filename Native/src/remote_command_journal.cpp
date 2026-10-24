#include "agentflow/remote_command_journal.hpp"
#include "agentflow/connection_profiles.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <limits>

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr auto category="remote-command-v1";
bool identifier(const std::string& v){return !v.empty()&&v.size()<=256&&v!="."&&v!=".."&&v.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-:")==std::string::npos;}
void valid(const RemoteSessionCreateIntent& i){
    if(!identifier(i.request_id)||!identifier(i.connection_id)||!identifier(i.server_id)||!identifier(i.scope_id)||i.profile_revision<1||i.credential_revision<1||i.title.size()>4096)throw std::invalid_argument("Invalid remote session creation intent");
    validate_remote_connection_origin(i.origin);
    // JSON encoding checks UTF-8 before any durable preparation.
    try{(void)Json(i.title).dump();}catch(const Json::exception&){throw std::invalid_argument("Invalid remote session title encoding");}
}
Json semantic(const RemoteSessionCreateIntent& i){return {{"operation","sessions.create"},{"connection_id",i.connection_id},{"origin",i.origin},{"server_id",i.server_id},{"scope_id",i.scope_id},{"profile_revision",i.profile_revision},{"credential_revision",i.credential_revision},{"title",i.title}};}
const char* state(RemoteCommandState s){switch(s){case RemoteCommandState::prepared:return "prepared";case RemoteCommandState::unknown:return "unknown";case RemoteCommandState::admitted:return "admitted";case RemoteCommandState::rejected:return "rejected";}throw DatabaseError("Invalid remote command state");}
std::string encode(const RemoteCommandRecord& r){return Json{{"version",1},{"request_id",r.intent.request_id},{"semantic",semantic(r.intent)},{"state",state(r.state)},{"revision",r.revision},{"resource_id",r.resource_id},{"resource_revision",r.resource_revision}}.dump();}
RemoteCommandRecord decode(const std::string& raw,const std::string& id){
    try{const auto j=Json::parse(raw);if(!j.is_object()||j.size()!=7||j.at("version")!=1||j.at("request_id")!=id||!j.at("revision").is_number_integer()||j.at("revision")<1)throw DatabaseError("Invalid remote command record");
        const auto& s=j.at("semantic");if(!s.is_object()||s.size()!=8||s.at("operation")!="sessions.create")throw DatabaseError("Invalid remote command record");
        RemoteCommandRecord r;r.intent={id,s.at("connection_id"),s.at("origin"),s.at("server_id"),s.at("scope_id"),s.at("title"),s.at("profile_revision"),s.at("credential_revision")};valid(r.intent);r.revision=j.at("revision");r.resource_id=j.at("resource_id");r.resource_revision=j.at("resource_revision");const auto status=j.at("state").get<std::string>();
        if(status=="prepared")r.state=RemoteCommandState::prepared;else if(status=="unknown")r.state=RemoteCommandState::unknown;else if(status=="admitted")r.state=RemoteCommandState::admitted;else if(status=="rejected")r.state=RemoteCommandState::rejected;else throw DatabaseError("Invalid remote command record");
        if(r.state==RemoteCommandState::admitted){if(!identifier(r.resource_id)||r.resource_revision<1||r.resource_revision>9007199254740991LL)throw DatabaseError("Invalid remote command record");}else if(!r.resource_id.empty()||r.resource_revision!=0)throw DatabaseError("Invalid remote command record");
        if(encode(r)!=raw)throw DatabaseError("Invalid remote command record");return r;
    }catch(const Json::exception&){throw DatabaseError("Invalid remote command record");}catch(const std::invalid_argument&){throw DatabaseError("Invalid remote command record");}
}
void matching(const RemoteCommandRecord& r,const RemoteSessionCreateIntent& i){if(r.intent.request_id!=i.request_id||semantic(r.intent)!=semantic(i))throw Conflict("Remote request ID belongs to a different intent");}
void advance(RemoteCommandRecord& r){if(r.revision==std::numeric_limits<std::int64_t>::max())throw Conflict("Remote command revision exhausted");++r.revision;}
}
RemoteCommandRecord RemoteCommandJournal::observe(const std::string& id){if(!identifier(id))throw std::invalid_argument("Invalid remote request identity");return decode(store_.information(category,id).get(),id);}
RemoteCommandRecord RemoteCommandJournal::prepare(const RemoteSessionCreateIntent& i){valid(i);RemoteCommandRecord r;r.intent=i;try{store_.compare_information(category,i.request_id,encode(r),std::nullopt).get();return r;}catch(const Conflict&){auto old=observe(i.request_id);matching(old,i);return old;}}
bool RemoteCommandJournal::claim_send(const RemoteSessionCreateIntent& i){valid(i);auto r=observe(i.request_id);matching(r,i);if(r.state!=RemoteCommandState::prepared)return false;const auto before=encode(r);r.state=RemoteCommandState::unknown;advance(r);try{store_.compare_information(category,i.request_id,encode(r),before).get();return true;}catch(const Conflict&){matching(observe(i.request_id),i);return false;}}
RemoteCommandRecord RemoteCommandJournal::resolve(const RemoteSessionCreateIntent& i,const RemoteCommandResolution& proof){
    valid(i);if(proof.request_id!=i.request_id||proof.server_id!=i.server_id||proof.scope_id!=i.scope_id||proof.operation!="sessions.create"||(proof.state!=RemoteCommandState::admitted&&proof.state!=RemoteCommandState::rejected))throw std::invalid_argument("Remote command resolution identity mismatch");
    if(proof.state==RemoteCommandState::admitted){if(!identifier(proof.resource_id)||proof.resource_revision<1||proof.resource_revision>9007199254740991LL)throw std::invalid_argument("Invalid admitted remote resource");}else if(!proof.resource_id.empty()||proof.resource_revision!=0)throw std::invalid_argument("Rejected remote request cannot name an admitted resource");
    auto r=observe(i.request_id);matching(r,i);if(r.state==proof.state&&r.resource_id==proof.resource_id&&r.resource_revision==proof.resource_revision)return r;if(r.state!=RemoteCommandState::unknown)throw Conflict("Remote command outcome cannot be replaced");const auto before=encode(r);r.state=proof.state;r.resource_id=proof.resource_id;r.resource_revision=proof.resource_revision;advance(r);store_.compare_information(category,i.request_id,encode(r),before).get();return r;
}
}
