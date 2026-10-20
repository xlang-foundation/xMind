#include "agentflow/graph.hpp"
#include "agentflow/agent_definitions.hpp"
#include "agentflow/authoring_document.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <set>
#include <functional>
namespace agentflow {
namespace {
using Json=nlohmann::json;constexpr std::int64_t maximum_revision=9007199254740991;
bool identifier(const std::string& value){return !value.empty() && value.size()<=64 && value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")==std::string::npos;}
Json parse(const std::string& source,bool stored){if(source.size()>(stored?1048576:262144))throw std::invalid_argument("Graph catalog exceeds limits");try{auto encoded=source;if(!stored){const auto first=source.find_first_not_of(" \t\r\n");if(first==std::string::npos)throw std::invalid_argument("Empty graph catalog");if(source[first]!='{'&&source[first]!='[')encoded=authoring_yaml_to_json(source);}return Json::parse(mcp_compact_object(encoded));}catch(const McpProtocolError&){throw std::invalid_argument("Invalid graph catalog JSON or YAML");}catch(const Json::exception&){throw std::invalid_argument("Invalid graph catalog JSON or YAML");}}
void fields(const Json& value,std::initializer_list<const char*> allowed){if(!value.is_object())throw std::invalid_argument("Graph catalog value must be an object");for(auto it=value.begin();it!=value.end();++it)if(std::none_of(allowed.begin(),allowed.end(),[&](const char* key){return it.key()==key;}))throw std::invalid_argument("Unknown or backend-owned graph catalog field");}
std::string id(const Json& value){if(!value.contains("id") || !value["id"].is_string() || !identifier(value["id"].get<std::string>()))throw std::invalid_argument("Invalid graph catalog ID");return value["id"].get<std::string>();}
std::int64_t revision(const Json& value,const char* key){if(!value.contains(key) || !value[key].is_number_integer() || value[key]<1 || value[key]>maximum_revision)throw std::invalid_argument("Invalid stored graph revision");return value[key].get<std::int64_t>();}
GraphCatalog decode(const Json& value,bool stored){
    if(stored)fields(value,{"version","revision","graphs","retired_ids"});else fields(value,{"graphs"});
    if(!value.contains("graphs") || !value["graphs"].is_array() || value["graphs"].size()>16)throw std::invalid_argument("Graph catalog accepts at most 16 definitions");
    GraphCatalog result;if(stored){if(!value.contains("version") || !value["version"].is_number_integer() || value["version"]!=1)throw std::invalid_argument("Invalid graph catalog version");result.revision=revision(value,"revision");}
    std::set<std::string> ids;
    for(const auto& record:value["graphs"]){if(stored)fields(record,{"id","revision","spec"});else fields(record,{"id","spec"});const auto key=id(record);if(!ids.insert(key).second || !record.contains("spec"))throw std::invalid_argument("Duplicate graph or missing specification");const auto rev=stored?revision(record,"revision"):0;if(rev>result.revision)throw std::invalid_argument("Graph revision exceeds catalog revision");result.entries.push_back({key,rev,GraphPlan(record["spec"].dump())});}
    if(stored){if(!value.contains("retired_ids") || !value["retired_ids"].is_array() || value["retired_ids"].size()>4096)throw std::invalid_argument("Invalid retired graph identities");for(const auto& retired:value["retired_ids"]){if(!retired.is_string())throw std::invalid_argument("Invalid retired graph identity");auto key=retired.get<std::string>();if(!identifier(key) || !ids.insert(key).second)throw std::invalid_argument("Duplicate or active retired graph identity");result.retired_ids.push_back(std::move(key));}}
    if(stored)for(const auto& entry:result.entries)for(const auto& node:entry.plan.nodes())if(node.graph){
        const auto target=std::find_if(result.entries.begin(),result.entries.end(),[&](const auto& candidate){return candidate.id==node.graph->graph_id;});
        if(target==result.entries.end()||target->revision!=node.graph->revision||target->plan.json()!=node.graph->specification_json)throw std::invalid_argument("Stored nested graph binding disagrees with its catalogue snapshot");
    }
    return result;
}
Json resolve_agent_definitions(Json value,const AgentDefinitionCatalog& definitions){
    if(!value.is_object()||!value.contains("graphs")||!value["graphs"].is_array())return value;
    for(auto& entry:value["graphs"]){
        if(!entry.is_object()||!entry.contains("spec")||!entry["spec"].is_object()||!entry["spec"].contains("nodes")||!entry["spec"]["nodes"].is_array())continue;
        for(auto& node:entry["spec"]["nodes"]){
            if(node.is_object()&&node.contains("type")&&node["type"].is_string()&&node["type"]=="graph")
                if(node.contains("graph_revision")||node.contains("graph_spec"))throw std::invalid_argument("Nested graph revision and snapshot are backend-owned");
            if(!node.is_object()||!node.contains("agent_id"))continue;
            if(!node["agent_id"].is_string()||node.contains("agent_revision")||node.contains("instructions")||node.contains("model_id")||node.contains("tools")||node.contains("permissions"))
                throw std::invalid_argument("Named graph agents cannot override or supply backend-pinned definition fields");
            const auto id=node["agent_id"].get<std::string>();
            const auto found=std::find_if(definitions.entries.begin(),definitions.entries.end(),[&](const auto& item){return item.id==id;});
            if(found==definitions.entries.end())throw std::invalid_argument("Graph references an unknown named agent definition");
            if(found->mode=="primary")throw std::invalid_argument("Primary-only agent definitions cannot be used as graph subagents");
            node["agent_revision"]=found->revision;
            node["instructions"]=found->instructions;
            if(!found->model_id.empty())node["model_id"]=found->model_id;
            if(found->tools)node["tools"]=*found->tools;
            if(!found->permissions.empty()){node["permissions"]=Json::array();for(const auto& rule:found->permissions)node["permissions"].push_back(Json{{"action",rule.action},{"resource",rule.resource},{"effect",rule.effect}});}
        }
    }
    return value;
}
GraphCatalog resolve_graph_definitions(const Json& value,const GraphCatalog& previous){
    fields(value,{"graphs"});
    if(!value.contains("graphs")||!value["graphs"].is_array()||value["graphs"].size()>16)throw std::invalid_argument("Graph catalog accepts at most 16 definitions");
    const auto& records=value["graphs"];std::vector<std::string> ids;
    for(const auto& record:records){
        fields(record,{"id","spec"});const auto key=id(record);
        if(std::find(ids.begin(),ids.end(),key)!=ids.end()||!record.contains("spec"))throw std::invalid_argument("Duplicate graph or missing specification");
        if(std::find(previous.retired_ids.begin(),previous.retired_ids.end(),key)!=previous.retired_ids.end())throw std::invalid_argument("Retired graph identities cannot be reused");
        ids.push_back(key);
    }
    std::vector<std::optional<GraphCatalogEntry>> compiled(records.size());
    std::vector<bool> visiting(records.size(),false);
    std::vector<std::size_t> expanded(records.size(),0),depth(records.size(),0);
    std::function<void(std::size_t)> compile=[&](std::size_t index){
        if(compiled[index])return;
        if(visiting[index])throw std::invalid_argument("Nested graph catalogue references must not be recursive");
        visiting[index]=true;auto spec=records[index]["spec"];
        if(!spec.is_object()||!spec.contains("nodes")||!spec["nodes"].is_array()||spec["nodes"].empty()||spec["nodes"].size()>100)throw std::invalid_argument("A graph requires 1-100 nodes");
        expanded[index]=spec["nodes"].size();
        for(auto& node:spec["nodes"]){
            if(!node.is_object()||!node.contains("type")||!node["type"].is_string()||node["type"]!="graph")continue;
            if(!node.contains("graph_id")||!node["graph_id"].is_string()||!identifier(node["graph_id"].get<std::string>()))throw std::invalid_argument("Nested graph requires a registered graph identity");
            const auto found=std::find(ids.begin(),ids.end(),node["graph_id"].get<std::string>());
            if(found==ids.end())throw std::invalid_argument("Nested graph references an unknown catalogue identity");
            const auto child=static_cast<std::size_t>(found-ids.begin());compile(child);
            if(expanded[child]>256-expanded[index])throw std::invalid_argument("Nested graph snapshot exceeds 256 total nodes");
            expanded[index]+=expanded[child];depth[index]=std::max(depth[index],1+depth[child]);
            if(depth[index]>3)throw std::invalid_argument("Nested graph snapshot depth exceeds three invocation levels");
            node["graph_revision"]=compiled[child]->revision;
            node["graph_spec"]=Json::parse(compiled[child]->plan.json());
            if(spec.dump().size()>262144)throw std::invalid_argument("Resolved graph snapshot exceeds limits");
        }
        GraphPlan plan(spec.dump());const auto old=std::find_if(previous.entries.begin(),previous.entries.end(),[&](const auto& entry){return entry.id==ids[index];});
        std::int64_t rev=1;
        if(old!=previous.entries.end()){
            rev=old->revision;
            if(old->plan.json()!=plan.json()){if(rev==maximum_revision)throw std::overflow_error("Graph revision exhausted");++rev;}
        }
        compiled[index].emplace(GraphCatalogEntry{ids[index],rev,std::move(plan)});visiting[index]=false;
    };
    GraphCatalog desired;
    for(std::size_t i=0;i<records.size();++i)compile(i);
    for(auto& entry:compiled)desired.entries.push_back(std::move(*entry));
    return desired;
}
std::string encode(const GraphCatalog& catalog){auto entries=Json::array();for(const auto& graph:catalog.entries)entries.push_back({{"id",graph.id},{"revision",graph.revision},{"spec",Json::parse(graph.plan.json())}});return Json{{"version",1},{"revision",catalog.revision},{"graphs",std::move(entries)},{"retired_ids",catalog.retired_ids}}.dump();}
}
GraphCatalog GraphCatalogStore::load(){std::string source;try{source=store_.information("native-graphs","catalog").get();}catch(const NotFound&){return {};};return decode(parse(source,true),true);}
GraphCatalog GraphCatalogStore::apply(const std::string& source){
    const auto definitions=AgentDefinitionStore(store_).load();
    const auto previous=load();auto desired=resolve_graph_definitions(resolve_agent_definitions(parse(source,false),definitions),previous);desired.retired_ids=previous.retired_ids;bool changed=previous.revision==0 || desired.entries.size()!=previous.entries.size();
    for(std::size_t i=0;i<desired.entries.size();++i){auto& entry=desired.entries[i];if(std::find(previous.retired_ids.begin(),previous.retired_ids.end(),entry.id)!=previous.retired_ids.end())throw std::invalid_argument("Retired graph identities cannot be reused");const auto found=std::find_if(previous.entries.begin(),previous.entries.end(),[&](const auto& old){return old.id==entry.id;});
        if(found==previous.entries.end()){entry.revision=1;changed=true;}
        else if(found->plan.json()==entry.plan.json()){entry.revision=found->revision;}
        else{if(found->revision==maximum_revision)throw std::overflow_error("Graph revision exhausted");entry.revision=found->revision+1;changed=true;}
        if(i>=previous.entries.size() || previous.entries[i].id!=entry.id)changed=true;
    }
    for(const auto& old:previous.entries)if(std::none_of(desired.entries.begin(),desired.entries.end(),[&](const auto& graph){return graph.id==old.id;})){if(desired.retired_ids.size()>=4096)throw std::overflow_error("Retired graph identity budget exhausted");desired.retired_ids.push_back(old.id);changed=true;}
    if(!changed)return previous;if(previous.revision==maximum_revision)throw std::overflow_error("Graph catalog revision exhausted");desired.revision=previous.revision+1;const auto encoded=encode(desired);if(encoded.size()>1048576)throw std::invalid_argument("Resolved graph catalog exceeds storage budget");store_.put_information("native-graphs","catalog",encoded).get();return desired;
}
}
