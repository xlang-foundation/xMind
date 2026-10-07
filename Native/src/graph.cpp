#include "agentflow/graph.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <map>
#include <set>
namespace agentflow {
namespace {
using Json=nlohmann::json;
Json object(const std::string& source,std::size_t limit=262144){
    if(source.size()>limit)throw std::invalid_argument("Graph JSON exceeds limits");
    try{return Json::parse(mcp_compact_object(source));}catch(const McpProtocolError&){throw std::invalid_argument("Invalid graph JSON");}catch(const Json::exception&){throw std::invalid_argument("Invalid graph JSON");}
}
void fields(const Json& value,std::initializer_list<const char*> allowed){
    if(!value.is_object())throw std::invalid_argument("Graph value must be an object");
    for(auto it=value.begin();it!=value.end();++it)if(std::none_of(allowed.begin(),allowed.end(),[&](const char* key){return it.key()==key;}))throw std::invalid_argument("Unknown graph field");
}
std::string text(const Json& value,const char* key,std::size_t max,bool empty=false){
    if(!value.contains(key) || !value[key].is_string())throw std::invalid_argument("Missing graph text field");
    auto result=value[key].get<std::string>();if((!empty && result.empty()) || result.size()>max || result.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid graph text field");return result;
}
bool identifier(const std::string& id){return !id.empty() && id.size()<=64 && id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")==std::string::npos;}
void path(const Json& value){
    if(!value.is_array() || value.size()>32)throw std::invalid_argument("Invalid graph value path");
    for(const auto& item:value)if(!((item.is_string() && item.get_ref<const std::string&>().size()<=256 && item.get_ref<const std::string&>().find('\0')==std::string::npos) || (item.is_number_integer() && item>=0 && item<=65535)))throw std::invalid_argument("Invalid graph path component");
}
void reference(const Json& ref,const std::vector<std::string>& dependencies,bool condition){
    if(condition)fields(ref,{"node","path","equals"});else fields(ref,{"node","path"});
    const auto id=text(ref,"node",64);if(std::find(dependencies.begin(),dependencies.end(),id)==dependencies.end())throw std::invalid_argument("Graph reference must name a declared dependency");
    if(ref.contains("path"))path(ref["path"]);if(condition && !ref.contains("equals"))throw std::invalid_argument("Graph condition requires equals");
}
void references(const Json& value,const std::vector<std::string>& dependencies){
    if(value.is_object() && value.contains("$ref")){if(value.size()!=1)throw std::invalid_argument("Graph reference cannot include literal fields");reference(value["$ref"],dependencies,false);return;}
    if(value.is_structured())for(const auto& item:value)references(item,dependencies);
}
Json at_path(Json value,const Json& parts){
    for(const auto& part:parts){
        if(part.is_string()){const auto key=part.get<std::string>();if(!value.is_object() || !value.contains(key))throw std::invalid_argument("Graph output path is unavailable");auto next=value.at(key);value=std::move(next);}
        else{const auto offset=part.get<std::size_t>();if(!value.is_array() || offset>=value.size())throw std::invalid_argument("Graph output index is unavailable");auto next=value.at(offset);value=std::move(next);}
    }return value;
}
Json resolve(const Json& value,const Json& outputs){
    if(value.is_object() && value.contains("$ref")){const auto& ref=value["$ref"];const auto id=ref.at("node").get<std::string>();if(!outputs.contains(id))throw std::invalid_argument("Graph dependency output is unavailable");return at_path(outputs.at(id),ref.value("path",Json::array()));}
    if(value.is_array()){auto result=Json::array();for(const auto& item:value)result.push_back(resolve(item,outputs));return result;}
    if(value.is_object()){auto result=Json::object();for(auto it=value.begin();it!=value.end();++it)result[it.key()]=resolve(it.value(),outputs);return result;}return value;
}
std::string name(GraphNodeState state){switch(state){case GraphNodeState::pending:return "pending";case GraphNodeState::running:return "running";case GraphNodeState::waiting_human:return "waiting_human";case GraphNodeState::completed:return "completed";case GraphNodeState::skipped:return "skipped";case GraphNodeState::failed:return "failed";case GraphNodeState::uncertain:return "uncertain";case GraphNodeState::cancelled:return "cancelled";}throw std::logic_error("Invalid graph state");}
GraphNodeState state_name(const std::string& value){for(const auto state:{GraphNodeState::pending,GraphNodeState::running,GraphNodeState::waiting_human,GraphNodeState::completed,GraphNodeState::skipped,GraphNodeState::failed,GraphNodeState::uncertain,GraphNodeState::cancelled})if(name(state)==value)return state;throw std::invalid_argument("Unknown graph node state");}
Json output_value(const std::string& source){
    if(source.size()>65536)throw std::invalid_argument("Graph output exceeds 64 KiB");
    // Wrap scalars in an object to reuse strict depth/duplicate-key validation.
    const auto wrapped=object("{\"value\":"+source+"}",65548);fields(wrapped,{"value"});return wrapped.at("value");
}
}
GraphPlan::GraphPlan(const std::string& source){
    const auto spec=object(source);fields(spec,{"nodes"});
    if(!spec.contains("nodes") || !spec["nodes"].is_array() || spec["nodes"].empty() || spec["nodes"].size()>100)throw std::invalid_argument("A graph requires 1-100 nodes");
    std::map<std::string,std::size_t> ids;
    for(const auto& value:spec["nodes"]){
        const auto kind=text(value,"type",16);GraphNodeDefinition node;node.id=text(value,"id",64);
        if(!identifier(node.id) || !ids.emplace(node.id,nodes_.size()).second)throw std::invalid_argument("Invalid or duplicate graph node ID");
        if(kind=="agent"){node.kind=GraphNodeKind::agent;fields(value,{"id","type","depends_on","prompt","model_id","when"});node.prompt=text(value,"prompt",32768);if(value.contains("model_id"))node.model_id=text(value,"model_id",256);}
        else if(kind=="tool"){node.kind=GraphNodeKind::tool;fields(value,{"id","type","depends_on","tool","arguments","when"});node.tool=text(value,"tool",128);if(!value.contains("arguments") || !value["arguments"].is_object())throw std::invalid_argument("Graph tool arguments must be an object");node.arguments_json=value["arguments"].dump();if(node.arguments_json.size()>65536)throw std::invalid_argument("Graph tool arguments exceed limits");}
        else if(kind=="human"){node.kind=GraphNodeKind::human;fields(value,{"id","type","depends_on","prompt","when"});node.prompt=text(value,"prompt",32768);}
        else throw std::invalid_argument("Unsupported graph node type");
        if(value.contains("depends_on")){const auto& dependencies=value["depends_on"];if(!dependencies.is_array() || dependencies.size()>100)throw std::invalid_argument("Invalid graph dependencies");std::set<std::string> unique;for(const auto& dependency:dependencies){if(!dependency.is_string())throw std::invalid_argument("Graph dependency must be an ID");auto id=dependency.get<std::string>();if(!identifier(id) || id==node.id || !unique.insert(id).second)throw std::invalid_argument("Invalid graph dependency");node.dependencies.push_back(std::move(id));}}
        if(value.contains("when")){reference(value["when"],node.dependencies,true);node.condition_json=value["when"].dump();}
        if(node.kind==GraphNodeKind::tool)references(value["arguments"],node.dependencies);nodes_.push_back(std::move(node));
    }
    for(const auto& node:nodes_)for(const auto& dependency:node.dependencies)if(!ids.contains(dependency))throw std::invalid_argument("Unknown graph dependency");
    std::set<std::string> visited;
    while(order_.size()<nodes_.size()){bool progress=false;for(std::size_t i=0;i<nodes_.size();++i){const auto& node=nodes_[i];if(visited.contains(node.id))continue;if(std::all_of(node.dependencies.begin(),node.dependencies.end(),[&](const auto& id){return visited.contains(id);})){visited.insert(node.id);order_.push_back(i);progress=true;}}if(!progress)throw std::invalid_argument("Graph cycles require a future bounded-loop execution contract");}
    json_=spec.dump();
}
std::size_t GraphCoordinator::index(const std::string& id) const {for(std::size_t i=0;i<plan_.nodes().size();++i)if(plan_.nodes()[i].id==id)return i;throw NotFound("Graph node not found");}
bool GraphCoordinator::eligible(std::size_t i,bool& skipped) const {
    skipped=false;const auto& node=plan_.nodes()[i];auto outputs=Json::object();
    for(const auto& dependency:node.dependencies){const auto& record=states_[index(dependency)];if(record.state==GraphNodeState::skipped){skipped=true;continue;}if(record.state!=GraphNodeState::completed)return false;outputs[dependency]=output_value(record.output);}
    if(!skipped && !node.condition_json.empty()){const auto condition=Json::parse(node.condition_json);skipped=at_path(outputs.at(condition.at("node").get<std::string>()),condition.value("path",Json::array()))!=condition.at("equals");}
    return true;
}
GraphCoordinator::GraphCoordinator(GraphPlan plan,const std::string& source):plan_(std::move(plan)),states_(plan_.nodes().size()){
    if(source.empty())return;const auto saved=object(source,1048576);fields(saved,{"version","spec","nodes"});
    if(!saved.contains("version") || !saved["version"].is_number_integer() || saved["version"]!=1 || !saved.contains("spec") || saved["spec"].dump()!=plan_.json() || !saved.contains("nodes") || !saved["nodes"].is_array() || saved["nodes"].size()!=states_.size())throw std::invalid_argument("Graph checkpoint does not match its immutable plan");
    std::set<std::string> seen;
    for(const auto& record:saved["nodes"]){fields(record,{"id","state","output"});const auto id=text(record,"id",64);if(!seen.insert(id).second)throw std::invalid_argument("Duplicate checkpoint node");const auto i=index(id);states_[i].state=state_name(text(record,"state",32));if(states_[i].state==GraphNodeState::completed){if(!record.contains("output"))throw std::invalid_argument("Completed graph node has no output");states_[i].output=record["output"].dump();output_value(states_[i].output);}else if(record.contains("output"))throw std::invalid_argument("Non-completed graph node cannot contain an output");}
    std::size_t bytes=0;for(const auto& record:states_)bytes+=record.output.size();if(bytes>524288)throw std::invalid_argument("Graph checkpoint output budget exceeded");
    for(const auto i:plan_.order()){const auto current=states_[i].state;if(current==GraphNodeState::pending || current==GraphNodeState::cancelled)continue;bool skipped=false;if(!eligible(i,skipped) || (current==GraphNodeState::skipped)!=skipped)throw std::invalid_argument("Graph checkpoint violates dependency or condition order");if(current==GraphNodeState::waiting_human && plan_.nodes()[i].kind!=GraphNodeKind::human)throw std::invalid_argument("Only a human node can await input");if(current==GraphNodeState::running && plan_.nodes()[i].kind==GraphNodeKind::human)throw std::invalid_argument("Human checkpoint must await input");}
    // Running work may already have had effects. Never replay it on restore.
    for(auto& record:states_)if(record.state==GraphNodeState::running)record.state=GraphNodeState::uncertain;
}
GraphDecision GraphCoordinator::inspect() const {
    GraphDecision result;result.finished=true;
    for(const auto& record:states_)if(record.state==GraphNodeState::failed || record.state==GraphNodeState::uncertain || record.state==GraphNodeState::cancelled)result.halted=true;
    for(std::size_t i=0;i<states_.size();++i){const auto current=states_[i].state;const auto& id=plan_.nodes()[i].id;if(current==GraphNodeState::failed || current==GraphNodeState::uncertain || current==GraphNodeState::cancelled)result.halted=true;
        if(current!=GraphNodeState::completed && current!=GraphNodeState::skipped)result.finished=false;
        if(current==GraphNodeState::waiting_human)result.waiting_human.push_back(id);if(current==GraphNodeState::running)result.running.push_back(id);
        if(current==GraphNodeState::pending && !result.halted){bool skipped=false;if(eligible(i,skipped))(skipped?result.skippable:result.ready).push_back(id);}
    }
    if(result.halted){result.ready.clear();result.skippable.clear();}return result;
}
GraphPreparedNode GraphCoordinator::start(const std::string& id){
    const auto decisions=inspect();if(std::find(decisions.ready.begin(),decisions.ready.end(),id)==decisions.ready.end())throw Conflict("Graph node is not ready");
    const auto i=index(id);auto node=plan_.nodes()[i];auto outputs=Json::object();for(const auto& dependency:node.dependencies)outputs[dependency]=output_value(states_[index(dependency)].output);
    if(node.kind==GraphNodeKind::tool){const auto args=resolve(Json::parse(node.arguments_json),outputs);if(!args.is_object())throw std::invalid_argument("Resolved graph arguments must be an object");node.arguments_json=args.dump();if(node.arguments_json.size()>65536)throw std::invalid_argument("Resolved graph tool arguments exceed limits");}
    states_[i].state=node.kind==GraphNodeKind::human?GraphNodeState::waiting_human:GraphNodeState::running;return {std::move(node),outputs.dump()};
}
void GraphCoordinator::skip(const std::string& id){const auto decisions=inspect();if(std::find(decisions.skippable.begin(),decisions.skippable.end(),id)==decisions.skippable.end())throw Conflict("Graph node is not skippable");states_[index(id)].state=GraphNodeState::skipped;}
void GraphCoordinator::output(std::size_t i,const std::string& source){const auto actual=output_value(source).dump();std::size_t bytes=actual.size();for(std::size_t n=0;n<states_.size();++n)if(n!=i)bytes+=states_[n].output.size();if(bytes>524288)throw std::invalid_argument("Graph output budget exceeded");states_[i].output=actual;states_[i].state=GraphNodeState::completed;}
void GraphCoordinator::complete(const std::string& id,const std::string& actual){const auto i=index(id);if(states_[i].state!=GraphNodeState::running)throw Conflict("Only an owned running graph node can complete");output(i,actual);}
void GraphCoordinator::provide_human(const std::string& id,const std::string& actual){const auto i=index(id);if(states_[i].state!=GraphNodeState::waiting_human)throw Conflict("Graph node is not waiting for human input");output(i,actual);}
void GraphCoordinator::fail(const std::string& id,bool uncertain){auto& record=states_[index(id)];if(record.state!=GraphNodeState::running)throw Conflict("Only running work can fail");record.state=uncertain?GraphNodeState::uncertain:GraphNodeState::failed;}
void GraphCoordinator::cancel_pending(){for(auto& record:states_)if(record.state==GraphNodeState::pending || record.state==GraphNodeState::waiting_human)record.state=GraphNodeState::cancelled;}
GraphNodeState GraphCoordinator::state(const std::string& id) const{return states_[index(id)].state;}
std::string GraphCoordinator::checkpoint() const{auto nodes=Json::array();for(std::size_t i=0;i<states_.size();++i){Json record={{"id",plan_.nodes()[i].id},{"state",name(states_[i].state)}};if(states_[i].state==GraphNodeState::completed)record["output"]=output_value(states_[i].output);nodes.push_back(std::move(record));}auto result=Json{{"version",1},{"spec",Json::parse(plan_.json())},{"nodes",std::move(nodes)}}.dump();if(result.size()>1048576)throw std::invalid_argument("Graph checkpoint exceeds limits");return result;}
}
