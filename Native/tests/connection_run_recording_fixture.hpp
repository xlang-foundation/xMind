#pragma once
#include "agentflow/connection_binding.hpp"
#include "agentflow/persistence_service.hpp"
#include "nlohmann/json.hpp"
namespace agentflow::test {
using Json=nlohmann::json;
struct RecordingExecutor:RunExecutor {
    PersistenceService& store;ConnectionRunRequest observed;std::string mode;unsigned calls=0;
    explicit RecordingExecutor(PersistenceService& value):store(value){}
    ExecutionWorkspaceMetadata execution_workspace()const override{return {true,"fixture-root","fixture-workspace",std::string(32,'a')};}
    Run record(std::string route,ConnectionRunRequest value){mode=std::move(route);observed=std::move(value);++calls;auto run=store.create_run(observed.id,observed.session).get();if(observed.provider)run.provider_context_json=Json{{"profile_id",observed.provider->id},{"route_id","fixture-route"},{"provider","fixture"},{"wire","chat-completions"},{"model_id",observed.model},{"profile_revision",observed.provider->revision}}.dump();return run;}
    Run submit(std::string id,std::string session,std::string prompt)override{return record("plain",{id,session,prompt});}
    Run submit_model(std::string id,std::string session,std::string prompt,std::string model)override{return record("model",{id,session,prompt,model});}
    Run submit_profile(std::string id,std::string session,std::string prompt,std::string model,ProviderProfileAdmission provider)override{ConnectionRunRequest request{id,session,prompt,model};request.provider=provider;return record("provider",std::move(request));}
    Run submit_workspace(std::string id,std::string session,std::string prompt,std::string model,WorkspaceAdmission workspace,std::optional<ProviderProfileAdmission> provider)override{ConnectionRunRequest request{id,session,prompt,model};request.workspace=workspace;request.provider=provider;return record("workspace",std::move(request));}
    Run submit_with_attachments(std::string id,std::string session,std::string prompt,std::string model,std::vector<PromptFileAttachment> files,std::optional<WorkspaceAdmission> workspace,std::optional<ProviderProfileAdmission> provider)override{ConnectionRunRequest request{id,session,prompt,model};request.files=std::move(files);request.workspace=workspace;request.provider=provider;return record("files",std::move(request));}
    Run submit_queued(std::string id,std::string session,std::string prompt,std::string model,std::string inbox,std::optional<WorkspaceAdmission> workspace,std::optional<ProviderProfileAdmission> provider,std::vector<PromptFileAttachment> files)override{ConnectionRunRequest request{id,session,prompt,model};request.inbox=inbox;request.files=std::move(files);request.workspace=workspace;request.provider=provider;return record("queued",std::move(request));}
    void cancel(const std::string& id)override{store.transition(id,RunState::queued,RunState::cancelled,"{}").get();}
    bool healthy()const override{return true;}
};
}
