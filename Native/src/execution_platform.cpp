#include "agentflow/execution_platform.hpp"
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>
namespace agentflow {
namespace {std::string workspace_nonce(){std::random_device random;std::ostringstream value;value<<std::hex<<std::setfill('0');for(int i=0;i<4;++i)value<<std::setw(8)<<random();return value.str();}}
ExecutionPlatform::ExecutionPlatform(PersistenceService& store,AgentSettings settings,std::size_t workers,std::size_t capacity):store_(store) {
    if(settings.workspace){workspace_binding_=std::make_unique<WorkspaceTools>(*settings.workspace);settings.workspace=workspace_binding_->root_path();workspace_authority_=workspace_nonce();}
    if(!settings.provider.model.empty())agents_=std::make_unique<AgentService>(store,settings,workers,capacity);
    graphs_=std::make_unique<GraphService>(store,std::move(settings),std::min<std::size_t>(workers,8),capacity);
}
ExecutionPlatform::~ExecutionPlatform()=default;
Run ExecutionPlatform::submit(std::string id,std::string session,std::string prompt){return submit_model(std::move(id),std::move(session),std::move(prompt),{});}
Run ExecutionPlatform::submit_model(std::string id,std::string session,std::string prompt,std::string model){
    if(!agents_)throw RunUnavailable("Configure a provider before running an agent");
    if(!healthy())throw RunUnavailable("Backend execution requires fault reconciliation");
    return agents_->submit_model(std::move(id),std::move(session),std::move(prompt),std::move(model));
}
Run ExecutionPlatform::submit_message(std::string id,std::string context,std::string message,std::string content,std::string identity){if(const auto replay=store_.incoming_message(message,context,identity,content).get())return *replay;if(!agents_||!healthy())throw RunUnavailable("Incoming agent execution is unavailable");return agents_->submit_message(std::move(id),std::move(context),std::move(message),std::move(content),std::move(identity));}
ExecutionWorkspaceMetadata ExecutionPlatform::execution_workspace()const{
    if(!workspace_binding_)return {};return {true,workspace_binding_->root_path(),workspace_binding_->identity(),workspace_authority_};
}
Run ExecutionPlatform::submit_workspace(std::string id,std::string session,std::string prompt,std::string model,WorkspaceAdmission expected,std::optional<ProviderProfileAdmission> profile){
    validate_workspace_admission(expected,execution_workspace());if(profile)throw RunUnavailable("Provider profile admission is unavailable");
    return submit_model(std::move(id),std::move(session),std::move(prompt),std::move(model));
}
Run ExecutionPlatform::submit_graph_workspace(std::string id,std::string session,std::string graph,std::int64_t revision,std::string prompt,std::string model,WorkspaceAdmission expected,std::optional<ProviderProfileAdmission> profile){
    validate_workspace_admission(expected,execution_workspace());if(profile)throw RunUnavailable("Graph provider profile admission is unavailable");
    return submit_graph(std::move(id),std::move(session),std::move(graph),revision,std::move(prompt),std::move(model));
}
std::vector<std::string> ExecutionPlatform::models() const{return agents_?agents_->models():std::vector<std::string>{};}
bool ExecutionPlatform::supports_delegation()const{return agents_&&healthy()&&agents_->supports_delegation();}
bool ExecutionPlatform::supports_dynamic_planning()const{return agents_&&healthy()&&agents_->supports_dynamic_planning();}
bool ExecutionPlatform::supports_context()const{return agents_&&healthy()&&agents_->supports_context();}
ContextControlSnapshot ExecutionPlatform::context_status(const std::string& session,const std::string& model)const{
    if(!agents_||!healthy())throw RunUnavailable("Native context controls are unavailable");return agents_->context_status(session,model);
}
ContextManualStatus ExecutionPlatform::context_request(const std::string& session,const std::string& request,const std::string& model)const{
    if(!agents_||!healthy())throw RunUnavailable("Native context controls are unavailable");return agents_->context_request(session,request,model);
}
ContextManualStatus ExecutionPlatform::request_context(const std::string& session,const std::string& request,const std::string& actor,std::int64_t revision,const std::string& model){
    if(!agents_||!healthy())throw RunUnavailable("Native context controls are unavailable");return agents_->request_context(session,request,actor,revision,model);
}
Run ExecutionPlatform::plan_input(const std::string& root,const std::string& request,std::string input,const std::string& actor,std::int64_t revision,std::int64_t sequence){
    if(!agents_||!healthy())throw RunUnavailable("Native dynamic input is unavailable");
    return agents_->plan_input(root,request,std::move(input),actor,revision,sequence);
}
Run ExecutionPlatform::resume_plan(const std::string& root,const std::string& actor,std::int64_t revision,std::int64_t sequence){
    if(!agents_||!healthy())throw RunUnavailable("Native dynamic resume is unavailable");
    return agents_->resume_plan(root,actor,revision,sequence);
}
void ExecutionPlatform::cancel(const std::string& id){
    const auto run=store_.run(id).get();
    if(run.graph_root){graphs_->cancel(id,"local-owner");return;}
    if(!run.parent_id.empty())throw Conflict("Graph children are controlled through their owning root");
    if(!agents_)throw RunUnavailable("Provider is not configured");agents_->cancel(id);
}
bool ExecutionPlatform::healthy() const{return (!agents_ || agents_->healthy()) && graphs_->healthy();}
bool ExecutionPlatform::available() const{return static_cast<bool>(agents_);}
bool ExecutionPlatform::idle() const{return (!agents_ || agents_->idle()) && graphs_->idle();}
std::vector<GraphExecutionMetadata> ExecutionPlatform::graphs() const{auto result=graphs_->graphs();if(!healthy())for(auto& graph:result)graph.executable=false;return result;}
Run ExecutionPlatform::submit_graph(std::string id,std::string session,std::string graph,std::int64_t revision,std::string prompt,std::string model){if(!healthy())throw RunUnavailable("Backend execution requires fault reconciliation");return graphs_->submit_graph(std::move(id),std::move(session),std::move(graph),revision,std::move(prompt),std::move(model));}
GraphRootRecord ExecutionPlatform::human_input(const std::string& root,const std::string& node,const std::string& input,const std::string& actor,std::int64_t revision){return graphs_->human_input(root,node,input,actor,revision);}
Run ExecutionPlatform::resume_graph(const std::string& root,const std::string& actor,std::int64_t revision){if(!healthy())throw RunUnavailable("Native graph resume is unavailable");return graphs_->resume_graph(root,actor,revision);}
GraphContextMetadata ExecutionPlatform::graph_context(const std::string& root)const{
    auto result=graphs_->graph_context(root);if(!healthy())result.resumable=false;return result;
}
}
