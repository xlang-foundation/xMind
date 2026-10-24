#pragma once
#include "agentflow/connection_binding.hpp"
#include "agentflow/persistence_service.hpp"
#include "agentflow/graph_service.hpp"
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
struct RecordingGraphExecutor:RecordingExecutor,GraphExecution {
    using RecordingExecutor::RecordingExecutor;
    ConnectionGraphRequest observed_graph;std::string graph_mode;unsigned graph_calls=0;
    std::vector<GraphExecutionMetadata> graphs()const override{return {{"fixture-graph",7,1,true}};}
    Run graph_record(std::string mode,ConnectionGraphRequest request){if(request.graph!="fixture-graph"||request.revision!=7)throw Conflict("Graph catalog revision changed");observed_graph=std::move(request);graph_mode=std::move(mode);++graph_calls;return store.start_graph_run(observed_graph.run.id,observed_graph.run.session,observed_graph.graph,observed_graph.revision,GraphPlan(R"({"nodes":[{"id":"worker","type":"agent","prompt":"recording fixture"}]})"),Json{{"content",observed_graph.run.prompt}}.dump()).get();}
    Run submit_graph(std::string id,std::string session,std::string graph,std::int64_t revision,std::string prompt,std::string model)override{return graph_record("plain",{{id,session,prompt,model},graph,revision});}
    Run submit_graph_profile(std::string id,std::string session,std::string graph,std::int64_t revision,std::string prompt,std::string model,ProviderProfileAdmission profile)override{ConnectionGraphRequest request{{id,session,prompt,model},graph,revision};request.run.provider=profile;return graph_record("provider",std::move(request));}
    Run submit_graph_workspace(std::string id,std::string session,std::string graph,std::int64_t revision,std::string prompt,std::string model,WorkspaceAdmission workspace,std::optional<ProviderProfileAdmission> profile)override{ConnectionGraphRequest request{{id,session,prompt,model},graph,revision};request.run.provider=profile;request.run.workspace=workspace;return graph_record("workspace",std::move(request));}
    Run submit_graph_workspace_with_attachments(std::string id,std::string session,std::string graph,std::int64_t revision,std::string prompt,std::string model,WorkspaceAdmission workspace,std::optional<ProviderProfileAdmission> profile,std::vector<PromptFileAttachment> files)override{ConnectionGraphRequest request{{id,session,prompt,model},graph,revision};request.run.provider=profile;request.run.workspace=workspace;request.run.files=std::move(files);return graph_record("files",std::move(request));}
    void cancel(const std::string& id)override{if(store.run(id).get().graph_root)store.retire_graph_run(id,RunState::cancelled,"{}").get();else RecordingExecutor::cancel(id);}
    GraphRootRecord human_input(const std::string&,const std::string&,const std::string&,const std::string&,std::int64_t)override{throw RunUnavailable("Recording graph peer has no human execution");}
};
}
