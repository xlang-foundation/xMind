#include "agentflow/remote_enrollment_approval.hpp"
#include "nlohmann/json.hpp"
#include <chrono>

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr auto category="remote-enrollment-local-approval-v1";
constexpr std::int64_t max_revision=9007199254740991LL;
bool identifier(const std::string& s){return !s.empty()&&s.size()<=256&&s!="."&&s!=".."&&s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-:")==std::string::npos;}
std::int64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
void binding_valid(const EnrollmentApprovalBinding& b){if(!identifier(b.connection_id)||!identifier(b.server_id)||!identifier(b.scope_id)||b.profile_revision<1||b.profile_revision>max_revision)throw std::invalid_argument("Invalid enrollment approval binding");}
void effects_valid(std::uint8_t effects){if(!effects||(effects&~7u))throw std::invalid_argument("Invalid enrollment effects");}
Json encode(const EnrollmentApproval& r){return {{"version",1},{"id",r.id},{"connection_id",r.binding.connection_id},{"server_id",r.binding.server_id},{"scope_id",r.binding.scope_id},{"profile_revision",r.binding.profile_revision},{"workspace_id",r.workspace_id},{"authority_id",r.authority_id},{"membership_revision",r.membership_revision},{"revision",r.revision},{"effects",r.effects},{"expires_unix_ms",r.expires_unix_ms},{"revoked",r.revoked}};}
EnrollmentApproval decode(const std::string& text,const std::string& id){
    try{const auto j=Json::parse(text);if(!j.is_object()||j.size()!=13||!j.at("version").is_number_integer()||j.at("version")!=1||!j.at("revoked").is_boolean())throw DatabaseError("Invalid enrollment approval");
        for(const auto* name:{"profile_revision","membership_revision","revision","effects","expires_unix_ms"})if(!j.at(name).is_number_integer())throw DatabaseError("Invalid enrollment approval integer");
        const auto effects=j.at("effects").get<std::int64_t>();if(effects<1||effects>7)throw DatabaseError("Invalid enrollment effects");
        EnrollmentApproval r;r.id=j.at("id");r.binding={j.at("connection_id"),j.at("server_id"),j.at("scope_id"),j.at("profile_revision")};r.workspace_id=j.at("workspace_id");r.authority_id=j.at("authority_id");r.membership_revision=j.at("membership_revision");r.revision=j.at("revision");r.effects=static_cast<std::uint8_t>(effects);r.expires_unix_ms=j.at("expires_unix_ms");r.revoked=j.at("revoked");
        binding_valid(r.binding);if(r.id!=id||!identifier(r.id)||!identifier(r.workspace_id)||!identifier(r.authority_id)||r.membership_revision<1||r.membership_revision>max_revision||r.revision<1||r.revision>max_revision||r.revision!=(r.revoked?2:1)||r.expires_unix_ms<1||r.expires_unix_ms>max_revision||encode(r)!=j)throw DatabaseError("Invalid enrollment approval record");return r;
    }catch(const DatabaseError&){throw;}catch(...){throw DatabaseError("Invalid persisted enrollment approval");}
}
void capture(RunExecutor& executor,EnrollmentApproval& record,std::int64_t expected){
    const auto workspace=executor.execution_workspace();const auto membership=executor.workspace_membership_snapshot();
    if(!workspace.configured||!identifier(workspace.workspace_id)||!identifier(workspace.authority_id)||membership.workspace_id!=workspace.workspace_id||membership.revision!=expected||expected<1||expected>max_revision||membership.folders.empty())throw Conflict("Enrollment workspace membership changed");
    record.workspace_id=workspace.workspace_id;record.authority_id=workspace.authority_id;record.membership_revision=membership.revision;
}
}
RemoteEnrollmentApprovals::RemoteEnrollmentApprovals(PersistenceService& store,RunExecutor& executor,EnrollmentApprovalBinding binding):store_(store),executor_(executor),binding_(std::move(binding)){binding_valid(binding_);}
EnrollmentApproval RemoteEnrollmentApprovals::observe(const std::string& id){if(!identifier(id))throw std::invalid_argument("Invalid enrollment approval identity");auto r=decode(store_.information(category,id).get(),id);if(r.binding!=binding_)throw Conflict("Enrollment approval belongs to another scope");return r;}
EnrollmentApproval RemoteEnrollmentApprovals::approve_from_owner(std::string id,std::int64_t expected,std::uint8_t effects,std::int64_t expires){
    if(!identifier(id)||expires<=now()||expires>max_revision)throw std::invalid_argument("Invalid enrollment approval");effects_valid(effects);
    EnrollmentApproval r;r.id=std::move(id);r.binding=binding_;r.effects=effects;r.expires_unix_ms=expires;capture(executor_,r,expected);
    // A reused identity cannot restore revoked authority or expand its effects.
    store_.compare_information(category,r.id,encode(r).dump(),std::nullopt).get();return r;
}
EnrollmentApproval RemoteEnrollmentApprovals::require_current(const std::string& id,std::uint8_t requested){
    effects_valid(requested);auto r=observe(id);if(r.revoked||r.expires_unix_ms<=now()||(requested&~r.effects))throw Conflict("Enrollment authority is inactive");
    auto current=r;capture(executor_,current,r.membership_revision);if(current.workspace_id!=r.workspace_id||current.authority_id!=r.authority_id)throw Conflict("Enrollment workspace authority changed");return r;
}
EnrollmentApproval RemoteEnrollmentApprovals::revoke(const std::string& id,std::int64_t revision){
    auto r=observe(id);if(r.revision!=revision||revision>=max_revision)throw Conflict("Enrollment approval revision changed");if(r.revoked)return r;
    const auto before=encode(r).dump();r.revoked=true;++r.revision;store_.compare_information(category,id,encode(r).dump(),before).get();return r;
}
}
