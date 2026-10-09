#pragma once
#include "agentflow/records.hpp"
#include "agentflow/context_control.hpp"
#include "agentflow/skill_records.hpp"
#include <utility>
#include <optional>
#include <vector>

namespace agentflow {
struct RunBusy : std::runtime_error {using std::runtime_error::runtime_error;};
struct RunUnavailable : std::runtime_error {using std::runtime_error::runtime_error;};
struct ProviderProfileAdmission {std::string id;std::int64_t revision=0;};
// Public workspace-generation binding. This nonce is not the private authority
// digest used by planning/context and contains no settings or credential data.
struct ExecutionWorkspaceMetadata {bool configured=false;std::string root,workspace_id,authority_id;};
struct WorkspaceSkillCatalogue {ExecutionWorkspaceMetadata workspace;std::string catalogue_json;};
struct WorkspaceAdmission {std::string workspace_id,authority_id;};
struct WorkspaceSessionSkills {ExecutionWorkspaceMetadata workspace;SessionSkillState state;};
inline void validate_workspace_admission(const WorkspaceAdmission& expected,const ExecutionWorkspaceMetadata& actual){
    if(expected.workspace_id.empty()||expected.workspace_id.size()>256||expected.authority_id.size()!=32||
       expected.authority_id.find_first_not_of("0123456789abcdef")!=std::string::npos)
        throw std::invalid_argument("Invalid workspace admission binding");
    if(!actual.configured||expected.workspace_id!=actual.workspace_id||expected.authority_id!=actual.authority_id)
        throw Conflict("Execution workspace changed before admission");
}
// Transport-neutral backend execution boundary; HTTP/CLI do not own run state.
class RunExecutor {
public:
    virtual ~RunExecutor()=default;
    virtual Run submit(std::string id,std::string session_id,std::string prompt)=0;
    virtual std::vector<std::string> models() const {return {};}
    virtual ExecutionWorkspaceMetadata execution_workspace()const{return {};}
    virtual bool supports_file_edit_proposals()const{return false;}
    virtual bool supports_skill_catalogue()const{return false;}
    virtual bool supports_session_skills()const{return false;}
    virtual WorkspaceSkillCatalogue workspace_skills()const{throw RunUnavailable("Workspace skill inspection is unavailable");}
    virtual WorkspaceSessionSkills session_skills(const std::string&)const{throw RunUnavailable("Session skill controls are unavailable");}
    virtual WorkspaceSessionSkills replace_session_skills(const std::string&,std::vector<std::string>,std::int64_t,WorkspaceAdmission){throw RunUnavailable("Session skill controls are unavailable");}
    virtual Run submit_workspace(std::string,std::string,std::string,std::string,WorkspaceAdmission,
        std::optional<ProviderProfileAdmission> = {}){throw RunUnavailable("Workspace-bound admission is unavailable");}
    virtual bool supports_profile_admission()const{return false;}
    virtual bool supports_delegation()const{return false;}
    virtual bool supports_dynamic_planning()const{return false;}
    virtual bool supports_context()const{return false;}
    virtual ContextControlSnapshot context_status(const std::string&,const std::string& = {})const{throw RunUnavailable("Context controls are unavailable");}
    virtual ContextManualStatus context_request(const std::string&,const std::string&,const std::string& = {})const{throw RunUnavailable("Context request inspection is unavailable");}
    virtual ContextManualStatus request_context(const std::string&,const std::string&,const std::string&,std::int64_t,const std::string& = {}){throw RunUnavailable("Context controls are unavailable");}
    virtual ContextManualStatus request_context_profile(const std::string&,const std::string&,const std::string&,std::int64_t,const std::string&,ProviderProfileAdmission){throw RunUnavailable("Context profile admission is unavailable");}
    virtual Run plan_input(const std::string&,const std::string&,std::string,const std::string&,std::int64_t,std::int64_t){throw RunUnavailable("Dynamic plan input is unavailable");}
    virtual Run resume_plan(const std::string&,const std::string&,std::int64_t,std::int64_t){throw RunUnavailable("Dynamic plan resume is unavailable");}
    virtual Run submit_profile(std::string,std::string,std::string,std::string,ProviderProfileAdmission){throw RunUnavailable("Provider profile admission is unavailable");}
    virtual Run submit_model(std::string id,std::string session_id,std::string prompt,std::string model_id) {
        if(!model_id.empty()) throw std::invalid_argument("Model selection is unavailable");
        return submit(std::move(id),std::move(session_id),std::move(prompt));
    }
    virtual Run submit_message(std::string,std::string,std::string,std::string,std::string){throw RunUnavailable("Incoming message admission is unavailable");}
    virtual void cancel(const std::string& id)=0;
    virtual bool healthy() const=0;
    virtual bool available() const {return true;}
};
}
