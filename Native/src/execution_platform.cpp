#include "agentflow/execution_platform.hpp"
#include <algorithm>
namespace agentflow {
ExecutionPlatform::ExecutionPlatform(PersistenceService& store,AgentSettings settings,std::size_t workers,std::size_t capacity):store_(store) {
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
std::vector<std::string> ExecutionPlatform::models() const{return agents_?agents_->models():std::vector<std::string>{};}
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
}
