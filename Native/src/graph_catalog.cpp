#include "agentflow/graph.hpp"
#include "agentflow/agent_definitions.hpp"
#include "agentflow/authoring_document.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <set>
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
    return result;
}
Json resolve_agent_definitions(Json value,const AgentDefinitionCatalog& definitions){
    if(!value.is_object()||!value.contains("graphs")||!value["graphs"].is_array())return value;
    for(auto& entry:value["graphs"]){
        if(!entry.is_object()||!entry.contains("spec")||!entry["spec"].is_object()||!entry["spec"].contains("nodes")||!entry["spec"]["nodes"].is_array())continue;
        for(auto& node:entry["spec"]["nodes"]){
            if(!node.is_object()||!node.contains("agent_id"))continue;
            if(!node["agent_id"].is_string()||node.contains("agent_revision")||node.contains("instructions")||node.contains("model_id"))
                throw std::invalid_argument("Named graph agents cannot override or supply backend-pinned definition fields");
            const auto id=node["agent_id"].get<std::string>();
            const auto found=std::find_if(definitions.entries.begin(),definitions.entries.end(),[&](const auto& item){return item.id==id;});
            if(found==definitions.entries.end())throw std::invalid_argument("Graph references an unknown named agent definition");
            node["agent_revision"]=found->revision;
            node["instructions"]=found->instructions;
            if(!found->model_id.empty())node["model_id"]=found->model_id;
        }
    }
    return value;
}
std::string encode(const GraphCatalog& catalog){auto entries=Json::array();for(const auto& graph:catalog.entries)entries.push_back({{"id",graph.id},{"revision",graph.revision},{"spec",Json::parse(graph.plan.json())}});return Json{{"version",1},{"revision",catalog.revision},{"graphs",std::move(entries)},{"retired_ids",catalog.retired_ids}}.dump();}
}
GraphCatalog GraphCatalogStore::load(){std::string source;try{source=store_.information("native-graphs","catalog").get();}catch(const NotFound&){return {};};return decode(parse(source,true),true);}
GraphCatalog GraphCatalogStore::apply(const std::string& source){
    const auto definitions=AgentDefinitionStore(store_).load();
    auto desired=decode(resolve_agent_definitions(parse(source,false),definitions),false);const auto previous=load();desired.retired_ids=previous.retired_ids;bool changed=previous.revision==0 || desired.entries.size()!=previous.entries.size();
    for(std::size_t i=0;i<desired.entries.size();++i){auto& entry=desired.entries[i];if(std::find(previous.retired_ids.begin(),previous.retired_ids.end(),entry.id)!=previous.retired_ids.end())throw std::invalid_argument("Retired graph identities cannot be reused");const auto found=std::find_if(previous.entries.begin(),previous.entries.end(),[&](const auto& old){return old.id==entry.id;});
        if(found==previous.entries.end()){entry.revision=1;changed=true;}
        else if(found->plan.json()==entry.plan.json()){entry.revision=found->revision;}
        else{if(found->revision==maximum_revision)throw std::overflow_error("Graph revision exhausted");entry.revision=found->revision+1;changed=true;}
        if(i>=previous.entries.size() || previous.entries[i].id!=entry.id)changed=true;
    }
    for(const auto& old:previous.entries)if(std::none_of(desired.entries.begin(),desired.entries.end(),[&](const auto& graph){return graph.id==old.id;})){if(desired.retired_ids.size()>=4096)throw std::overflow_error("Retired graph identity budget exhausted");desired.retired_ids.push_back(old.id);changed=true;}
    if(!changed)return previous;if(previous.revision==maximum_revision)throw std::overflow_error("Graph catalog revision exhausted");desired.revision=previous.revision+1;store_.put_information("native-graphs","catalog",encode(desired)).get();return desired;
}
}
