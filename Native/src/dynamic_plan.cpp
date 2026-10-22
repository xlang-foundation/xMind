#include "agentflow/dynamic_plan.hpp"
#include "agentflow/mcp_wire.hpp"
#include "agentflow/json_schema.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <set>

namespace agentflow {
std::vector<std::string> dynamic_preset_tools(const DynamicPresetCapability& preset,const std::vector<ModelToolDefinition>& offered){
    if(preset.revision!=1||(preset.id!="workspace.inspect"&&preset.id!="workspace.coding")||preset.readonly!=(preset.id=="workspace.inspect"))throw DynamicPlanUnavailable("Unsupported native preset boundary");
    const std::set<std::string> readonly{"read_file","list_files","search_files","find_files","glob_files","read_repository_instructions","list_skills","load_skill"};
    const std::set<std::string> orchestration{"delegate_tasks","plan_tasks","revise_plan","inspect_plan"};std::vector<std::string> result;
    for(const auto& tool:offered)if(!orchestration.contains(tool.name)&&(!preset.readonly||readonly.contains(tool.name)))result.push_back(tool.name);
    std::sort(result.begin(),result.end());if(std::adjacent_find(result.begin(),result.end())!=result.end())throw DynamicPlanUnavailable("Duplicate offered preset tool");return result;
}
std::string seal_dynamic_tool_catalogue(const std::vector<ModelToolDefinition>& definitions,const std::string& approval_bindings_json){
    using Json=nlohmann::json;const auto actual_bindings=Json::parse(approval_bindings_json);
    if(!actual_bindings.is_array())throw DynamicPlanUnavailable("Actual external-agent approval bindings are not a native array");
    std::map<std::string,Json> bindings;
    for(const auto& binding:actual_bindings){
        if(!binding.is_object()||!binding.contains("alias")||!binding["alias"].is_string()||
            !bindings.emplace(binding["alias"].get<std::string>(),binding).second)
            throw DynamicPlanUnavailable("Actual external-agent approval bindings contain an invalid or duplicate alias");
    }
    auto sorted=definitions;std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.name<b.name;});
    Json result={{"tools",Json::array()}};std::string previous;std::size_t matched_bindings=0;
    for(const auto& definition:sorted){
        if(definition.name.empty()||definition.name==previous)throw DynamicPlanUnavailable("Actual tool catalogue contains duplicate identities");
        previous=definition.name;
        // Retain exact schema text, including numeric/escaped-key lexemes.
        Json captured={{"name",definition.name},{"description",definition.description},{"input_schema_json",definition.input_schema_json}};
        const auto binding=bindings.find(definition.name);
        if(binding!=bindings.end()){
            if(!binding->second.contains("input_schema_json")||binding->second["input_schema_json"]!=definition.input_schema_json)
                throw DynamicPlanUnavailable("Actual external-agent approval binding differs from its native tool schema");
            if(binding->second.value("kind",std::string{})=="trusted_xlang3_callable")captured["xlang3_binding"]=binding->second;
            else if(definition.name.starts_with("mcp_"))captured["mcp_binding"]=binding->second;
            else if(definition.name.starts_with("a2a_delegate_")&&binding->second.value("kind",std::string{})=="a2a_remote_agent")captured["a2a_binding"]=binding->second;
            else throw DynamicPlanUnavailable("External-agent binding has an unexpected tool namespace");
            ++matched_bindings;
        }else if(definition.name.starts_with("mcp_")||definition.name.starts_with("a2a_delegate_"))throw DynamicPlanUnavailable("External-agent tool has no native approval binding");
        result["tools"].push_back(std::move(captured));
    }
    if(matched_bindings!=bindings.size())throw DynamicPlanUnavailable("Actual MCP approval binding has no discovered native tool");
    return result.dump();
}
namespace {
using Json=nlohmann::json;
constexpr std::int64_t safe_integer=9007199254740991;
constexpr std::size_t change_limit=262144,outcome_limit=32768,report_limit=65536;
Json object(const std::string& source,std::size_t limit){
    if(source.empty()||source.size()>limit)throw std::invalid_argument("Dynamic plan JSON exceeds its byte limit");
    try{return Json::parse(mcp_compact_object(source));}
    catch(const McpProtocolError&){throw std::invalid_argument("Invalid dynamic plan JSON, duplicate field or UTF-8");}
    catch(const Json::exception&){throw std::invalid_argument("Invalid dynamic plan JSON");}
}
void fields(const Json& value,std::initializer_list<const char*> allowed){
    if(!value.is_object())throw std::invalid_argument("Dynamic plan value must be an object");
    for(auto it=value.begin();it!=value.end();++it){bool known=false;for(const auto* field:allowed)known|=it.key()==field;
        if(!known)throw std::invalid_argument("Unsupported dynamic plan field");}
}
std::string string(const Json& value,const char* key,std::size_t limit){
    if(!value.contains(key)||!value[key].is_string())throw std::invalid_argument("Dynamic plan requires a string field");
    auto result=value[key].get<std::string>();
    if(result.empty()||result.size()>limit||result.find('\0')!=std::string::npos)throw std::invalid_argument(std::string("Dynamic plan string field exceeds its bounds: ")+key);
    return result;
}
void valid_text(const std::string& value,std::size_t limit){
    if(value.empty()||value.size()>limit||value.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid dynamic plan text");
    try{(void)Json(value).dump();}catch(const Json::exception&){throw std::invalid_argument("Invalid dynamic plan UTF-8");}
}
void label(const std::string& value){
    valid_text(value,32);
    if(value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)
        throw std::invalid_argument("Invalid dynamic task label");
}
void identity(const std::string& value){
    valid_text(value,128);if(value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::invalid_argument("Invalid backend dynamic execution identity");
}
std::int64_t integer(const Json& value,const char* key,std::int64_t minimum){
    if(!value.contains(key)||!value[key].is_number_integer())throw std::invalid_argument("Dynamic plan requires an exact integer precondition");
    const auto& number=value[key];
    if(number.is_number_unsigned()&&number.get<std::uint64_t>()>static_cast<std::uint64_t>(safe_integer))throw std::invalid_argument("Dynamic precondition exceeds the exact integer range");
    const auto result=number.get<std::int64_t>();
    if(result<minimum||result>safe_integer)throw std::invalid_argument("Dynamic plan precondition is outside its bounds");return result;
}
const char* kind(DynamicNodeKind value){
    switch(value){case DynamicNodeKind::agent:return "agent";case DynamicNodeKind::tool:return "tool";case DynamicNodeKind::human:return "human";}
    throw std::invalid_argument("Unsupported dynamic node kind");
}
const char* requirement(DynamicDependencyRequirement value){
    switch(value){case DynamicDependencyRequirement::success:return "success";case DynamicDependencyRequirement::observed:return "observed";}
    throw std::invalid_argument("Unsupported dynamic dependency requirement");
}
const char* state(DynamicNodeState value){
    switch(value){case DynamicNodeState::pending:return "pending";case DynamicNodeState::blocked:return "blocked";case DynamicNodeState::claimed:return "claimed";
    case DynamicNodeState::waiting_human:return "waiting_human";case DynamicNodeState::settled:return "settled";case DynamicNodeState::skipped:return "skipped";
    case DynamicNodeState::cancelled:return "cancelled";case DynamicNodeState::uncertain:return "uncertain";}
    throw std::invalid_argument("Unsupported dynamic node state");
}
Json definition_json(const DynamicNodeDefinition& node){
    Json result={{"id",node.label},{"type",kind(node.kind)},{"depends_on",Json::array()}};
    if(node.kind==DynamicNodeKind::agent){result["objective"]=node.objective;result["preset"]=node.preset_id;}
    else if(node.kind==DynamicNodeKind::tool){result["tool"]=node.tool;result["arguments_json"]=node.arguments_json;}
    else result["question"]=node.question;
    for(const auto& dependency:node.dependencies)result["depends_on"].push_back({{"task",dependency.task},{"require",requirement(dependency.require)}});
    return result;
}
bool same_definition(const DynamicNodeDefinition& left,const DynamicNodeDefinition& right){
    return left.objective==right.objective&&left.question==right.question&&left.preset_id==right.preset_id&&left.preset_revision==right.preset_revision&&left.tool==right.tool&&left.arguments_json==right.arguments_json&&definition_json(left)==definition_json(right);
}
DynamicNodeDefinition node(const Json& value){
    const auto type=string(value,"type",16);DynamicNodeDefinition result;result.label=string(value,"id",32);label(result.label);
    if(type=="agent"){
        fields(value,{"id","type","objective","preset","depends_on"});result.kind=DynamicNodeKind::agent;
        result.objective=string(value,"objective",8192);result.preset_id=string(value,"preset",64);result.preset_revision=1;
        if(result.preset_id!="workspace.inspect"&&result.preset_id!="workspace.coding")throw std::invalid_argument("Unsupported dynamic agent preset");
    }else if(type=="tool"){
        fields(value,{"id","type","tool","arguments_json","depends_on"});result.kind=DynamicNodeKind::tool;
        result.tool=string(value,"tool",128);result.arguments_json=string(value,"arguments_json",65536);
        result.preset_id.clear();result.preset_revision=0;
        (void)object(result.arguments_json,65536);
    }else if(type=="human"){
        fields(value,{"id","type","question","depends_on"});result.kind=DynamicNodeKind::human;result.question=string(value,"question",8192);
        result.preset_id.clear();result.preset_revision=0;
    }else throw std::invalid_argument("Unsupported native dynamic node type");
    if(!value.contains("depends_on")||!value["depends_on"].is_array()||value["depends_on"].size()>32)throw std::invalid_argument("Dynamic dependencies require a bounded array");
    std::set<std::string> unique;
    for(const auto& item:value["depends_on"]){
        fields(item,{"task","require"});DynamicDependency dependency;dependency.task=string(item,"task",32);label(dependency.task);
        if(dependency.task==result.label||!unique.insert(dependency.task).second)throw std::invalid_argument("Duplicated or self-referential dynamic dependency");
        const auto expected=string(item,"require",8);
        if(expected=="success")dependency.require=DynamicDependencyRequirement::success;
        else if(expected=="observed")dependency.require=DynamicDependencyRequirement::observed;
        else throw std::invalid_argument("Unsupported dynamic dependency requirement");
        result.dependencies.push_back(std::move(dependency));
    }return result;
}
void capabilities(const DynamicPlanCapabilities& value){
    if(value.revision<1||value.revision>safe_integer||value.backend_identity.size()!=64||value.backend_identity.find_first_not_of("0123456789abcdef")!=std::string::npos||
       value.max_nodes<1||value.max_nodes>32||value.max_revisions<1||value.max_revisions>16||value.max_humans<1||value.max_humans>8||
       value.change_bytes<1||value.change_bytes>static_cast<std::int64_t>(change_limit)||value.human_expiry_ms<1||value.human_expiry_ms>3600000||value.max_parent_turns<1||value.max_parent_turns>32)
        throw std::invalid_argument("Invalid admitted dynamic plan policy");
    valid_text(value.workspace_identity,4096);(void)object(value.provider_identity_json,4096);(void)object(value.tool_catalog_json,change_limit);
    if(value.presets.empty()||value.presets.size()>2)throw std::invalid_argument("Invalid registered dynamic presets");
    std::set<std::string> ids;
    for(const auto& preset:value.presets){
        if((preset.id!="workspace.inspect"&&preset.id!="workspace.coding")||preset.revision!=1||!ids.insert(preset.id).second||preset.readonly!=(preset.id=="workspace.inspect")||preset.turn_limit<1||preset.turn_limit>(preset.readonly?4:16))throw std::invalid_argument("Invalid registered dynamic preset binding");
        if(preset.backend_identity.size()!=64||preset.backend_identity.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::invalid_argument("Dynamic preset lacks its private immutable settings identity");
        if(preset.tools.size()>128)throw std::invalid_argument("Invalid registered dynamic tool catalogue");
        std::set<std::string> tools;
        for(const auto& tool:preset.tools){valid_text(tool,128);if(!tools.insert(tool).second)throw std::invalid_argument("Duplicate registered dynamic tool");
            if(preset.readonly&&tool!="read_repository_instructions"&&tool!="read_file"&&tool!="list_files"&&tool!="search_files"&&tool!="find_files"&&tool!="glob_files"&&tool!="list_skills"&&tool!="load_skill")throw std::invalid_argument("Read-only dynamic preset contains an effect tool");
            if(tool=="delegate_tasks"||tool=="plan_tasks"||tool=="revise_plan"||tool=="inspect_plan")throw std::invalid_argument("Dynamic child recursion is not supported");
        }
    }
}
bool same_capabilities(const DynamicPlanCapabilities& left,const DynamicPlanCapabilities& right){
    if(left.revision!=right.revision||left.backend_identity!=right.backend_identity||left.workspace_identity!=right.workspace_identity||left.provider_identity_json!=right.provider_identity_json||
       left.tool_catalog_json!=right.tool_catalog_json||left.max_nodes!=right.max_nodes||left.max_revisions!=right.max_revisions||left.max_humans!=right.max_humans||
       left.change_bytes!=right.change_bytes||left.human_expiry_ms!=right.human_expiry_ms||left.max_parent_turns!=right.max_parent_turns||left.catalogue_finalized!=right.catalogue_finalized||left.presets.size()!=right.presets.size())return false;
    for(std::size_t i=0;i<left.presets.size();++i){const auto& a=left.presets[i];const auto& b=right.presets[i];if(a.id!=b.id||a.revision!=b.revision||a.readonly!=b.readonly||a.tools!=b.tools||a.turn_limit!=b.turn_limit||a.backend_identity!=b.backend_identity)return false;}return true;
}
void definition(const DynamicNodeDefinition& value,const DynamicPlanCapabilities& frozen){
    const auto parsed=node(definition_json(value));if(!same_definition(parsed,value))throw std::invalid_argument("Dynamic definition contains unsupported fields or preset revision");
    if(value.kind==DynamicNodeKind::agent&&std::none_of(frozen.presets.begin(),frozen.presets.end(),[&](const auto& preset){return preset.id==value.preset_id&&preset.revision==value.preset_revision;}))throw std::invalid_argument("Requested dynamic preset is unavailable");
    if(value.kind==DynamicNodeKind::tool){
        static const std::set<std::string> deterministic_reads={"read_repository_instructions","read_file","list_files","search_files","find_files","glob_files"};
        const auto catalogue=object(frozen.tool_catalog_json,change_limit);const auto found=std::find_if(catalogue.at("tools").begin(),catalogue.at("tools").end(),[&](const auto& item){return item.is_object()&&item.value("name",std::string{})==value.tool;});
        if(found==catalogue.at("tools").end()||!found->contains("input_schema_json")||!found->at("input_schema_json").is_string())throw std::invalid_argument("Dynamic deterministic tool lacks its sealed native schema");
        if(deterministic_reads.contains(value.tool)){
            const auto inspector=std::find_if(frozen.presets.begin(),frozen.presets.end(),[](const auto& preset){return preset.id=="workspace.inspect"&&preset.revision==1;});
            if(inspector==frozen.presets.end()||!inspector->readonly||std::find(inspector->tools.begin(),inspector->tools.end(),value.tool)==inspector->tools.end())throw std::invalid_argument("Dynamic deterministic tool is outside the admitted read-only preset");
        }else if(value.tool=="run_process"){
            const auto coding=std::find_if(frozen.presets.begin(),frozen.presets.end(),[](const auto& preset){return preset.id=="workspace.coding"&&preset.revision==1&&!preset.readonly;});
            if(coding==frozen.presets.end()||std::find(coding->tools.begin(),coding->tools.end(),value.tool)==coding->tools.end()||found->contains("mcp_binding"))throw std::invalid_argument("Dynamic process tool is outside the admitted coding preset");
            const auto arguments=object(value.arguments_json,65536);if(arguments.contains("background")&&(!arguments["background"].is_boolean()||arguments["background"].get<bool>()))throw std::invalid_argument("Dynamic process nodes must finish within their owned child");
            if(arguments.contains("folder_id")&&(!arguments["folder_id"].is_string()||arguments["folder_id"]!=frozen.workspace_identity))throw std::invalid_argument("Dynamic process nodes are restricted to the admitted primary workspace folder");
        }else if(value.tool.starts_with("a2a_delegate_")){
            const auto coding=std::find_if(frozen.presets.begin(),frozen.presets.end(),[](const auto& preset){return preset.id=="workspace.coding"&&preset.revision==1&&!preset.readonly;});
            if(coding==frozen.presets.end()||std::find(coding->tools.begin(),coding->tools.end(),value.tool)==coding->tools.end()||!found->contains("a2a_binding")||!found->at("a2a_binding").is_object())throw std::invalid_argument("Dynamic A2A tool is outside the admitted coding preset");
            const auto& binding=found->at("a2a_binding");for(const auto* key:{"kind","alias","input_schema_json","agent_id","agent_revision","card_url","protocol_version","endpoint","agent_name","agent_version","skills_json","has_bearer"})if(!binding.contains(key))throw std::invalid_argument("Dynamic A2A tool lacks its complete sealed binding");
            if(binding.at("kind")!="a2a_remote_agent"||binding.at("alias")!=value.tool||binding.at("input_schema_json")!=found->at("input_schema_json")||binding.at("agent_id").get<std::string>().empty()||!binding.at("agent_revision").is_number_integer()||binding.at("agent_revision").get<std::int64_t>()<1||!binding.at("has_bearer").is_boolean())throw std::invalid_argument("Dynamic A2A binding differs from its sealed tool schema");
        }else{
            const auto coding=std::find_if(frozen.presets.begin(),frozen.presets.end(),[](const auto& preset){return preset.id=="workspace.coding"&&preset.revision==1&&!preset.readonly;});
            if(coding==frozen.presets.end()||std::find(coding->tools.begin(),coding->tools.end(),value.tool)==coding->tools.end()||!value.tool.starts_with("mcp_")||!found->contains("mcp_binding")||!found->at("mcp_binding").is_object())throw std::invalid_argument("Dynamic MCP node must be sealed in the coding preset");
            const auto& binding=found->at("mcp_binding");
            for(const auto* key:{"server_config_id","config_revision","alias","catalogue_fingerprint","protocol_version","input_schema_json","output_schema_json","annotations_json"})if(!binding.contains(key))throw std::invalid_argument("Dynamic MCP tool lacks its complete sealed binding");
            if(binding.at("alias")!=value.tool||binding.at("input_schema_json")!=found->at("input_schema_json"))throw std::invalid_argument("Dynamic MCP binding differs from its sealed tool schema");
            if(binding.contains("kind")){
                const auto kind=binding.at("kind");if(!kind.is_string()||(kind!="resource"&&kind!="prompt")||!binding.contains("peer_name")||!binding.at("peer_name").is_string()||!binding.contains("metadata_json")||!binding.at("metadata_json").is_string())throw std::invalid_argument("Dynamic MCP read binding is incomplete or unsupported");
                if(kind=="resource"&&(!binding.contains("resource_uri")||!binding.at("resource_uri").is_string()))throw std::invalid_argument("Dynamic MCP resource binding lacks its exact URI");
                if(kind=="prompt"&&binding.contains("resource_uri"))throw std::invalid_argument("Dynamic MCP prompt binding cannot contain a resource URI");
            }else if(!binding.contains("peer_tool")||!binding.at("peer_tool").is_string())throw std::invalid_argument("Dynamic MCP effect binding lacks its exact peer tool");
        }
        JsonSchema schema(found->at("input_schema_json").get<std::string>());schema.validate_object(value.arguments_json);
    }
}
void topology(const std::vector<DynamicNodeRecord>& nodes){
    std::map<std::string,std::size_t> indexes;
    for(std::size_t i=0;i<nodes.size();++i)if(!indexes.emplace(nodes[i].definition.label,i).second)throw std::invalid_argument("Duplicated dynamic task label");
    std::vector<unsigned char> marks(nodes.size(),0);
    std::function<void(std::size_t)> visit=[&](std::size_t index){
        if(marks[index]==1)throw std::invalid_argument("Dynamic plan dependencies contain a cycle");if(marks[index]==2)return;marks[index]=1;
        for(const auto& dependency:nodes[index].definition.dependencies){const auto found=indexes.find(dependency.task);if(found==indexes.end())throw std::invalid_argument("Unknown dynamic dependency");visit(found->second);}marks[index]=2;
    };
    for(std::size_t i=0;i<nodes.size();++i)visit(i);
}
bool protected_node(const DynamicNodeRecord& value){
    return value.protected_definition||value.claim_revision!=0||!value.claim_id.empty()||!value.human_request_id.empty()||!value.child_run_id.empty()||!value.child_state.empty()||!value.outcome_json.empty()||value.settled_event_seq.has_value()||
       (!value.effect_state.empty()&&value.effect_state!="none")||(value.state!=DynamicNodeState::pending&&value.state!=DynamicNodeState::blocked);
}
void snapshot(const DynamicPlanRecord& value,bool initial=false){
    capabilities(value.capabilities);identity(value.root_run_id);
    if(value.state!="active"&&value.state!="waiting_human"&&value.state!="completed"&&value.state!="failed"&&value.state!="cancelled"&&value.state!="interrupted"&&value.state!="uncertain")throw std::invalid_argument("Unsupported dynamic plan lifecycle");
    if(value.revision<0||value.revision>value.capabilities.max_revisions||value.state_sequence<0||value.state_sequence>safe_integer||value.nodes.size()>static_cast<std::size_t>(value.capabilities.max_nodes)||value.humans_published<0||value.humans_published>value.capabilities.max_humans)throw std::invalid_argument("Invalid dynamic plan snapshot bounds");
    if(value.revision==0){if(!initial||value.state_sequence!=0||!value.nodes.empty()||!value.retired_labels.empty()||value.humans_published!=0||value.planned_children_reserved!=0||value.planned_humans_reserved!=0)throw std::invalid_argument("Invalid initial dynamic plan snapshot");return;}
    if(!value.capabilities.catalogue_finalized)throw std::invalid_argument("Executable dynamic plan lacks its finalized native catalogue");
    identity(value.id);if(value.state_sequence<1)throw std::invalid_argument("Dynamic snapshot lacks its state sequence");
    if(value.nodes.empty())throw std::invalid_argument("Dynamic plan has no definitions");
    std::set<std::string> backend_ids,retired;
    for(const auto& item:value.retired_labels){label(item);if(!retired.insert(item).second)throw std::invalid_argument("Duplicate retired dynamic label");}
    std::int64_t published_humans=0,pending_humans=0,pending_children=0;
    for(const auto& item:value.nodes){
        definition(item.definition,value.capabilities);(void)state(item.state);identity(item.backend_node_id);
        for(const auto* id:{&item.claim_id,&item.child_run_id,&item.human_request_id})if(!id->empty())identity(*id);
        if(!backend_ids.insert(item.backend_node_id).second||item.definition_revision<1||item.definition_revision>value.revision||item.claim_revision<0||item.claim_revision>value.revision)throw std::invalid_argument("Invalid immutable dynamic node identity");
        if(item.definition.kind==DynamicNodeKind::human){if(!item.human_request_id.empty())++published_humans;else if(item.state==DynamicNodeState::pending||item.state==DynamicNodeState::blocked)++pending_humans;}
        else if(item.state==DynamicNodeState::pending||item.state==DynamicNodeState::blocked)++pending_children;
        if(retired.contains(item.definition.label)!=(item.state==DynamicNodeState::skipped))throw std::invalid_argument("Dynamic retired label differs from its recorded state");
        if((item.state==DynamicNodeState::pending||item.state==DynamicNodeState::blocked||item.state==DynamicNodeState::skipped)&&
           (item.claim_revision!=0||!item.claim_id.empty()||!item.human_request_id.empty()||!item.child_run_id.empty()||!item.child_state.empty()||!item.outcome_json.empty()||item.settled_event_seq||(!item.effect_state.empty()&&item.effect_state!="none")))throw std::invalid_argument("Unclaimed dynamic node carries actual execution data");
        if(item.claim_revision>0&&(item.claim_revision!=item.definition_revision||item.claim_id.empty()))throw std::invalid_argument("Dynamic claim does not bind its immutable definition");
        if(item.state==DynamicNodeState::waiting_human&&(item.definition.kind!=DynamicNodeKind::human||item.claim_revision==0||item.human_request_id.empty()))throw std::invalid_argument("Invalid published dynamic human node");
        if(item.state==DynamicNodeState::claimed&&((item.definition.kind!=DynamicNodeKind::agent&&item.definition.kind!=DynamicNodeKind::tool)||item.claim_revision==0||item.child_run_id.empty()))throw std::invalid_argument("Invalid actual dynamic child claim");
        if(item.definition.kind==DynamicNodeKind::human&&(!item.child_run_id.empty()||!item.child_state.empty()||(!item.effect_state.empty()&&item.effect_state!="none")))throw std::invalid_argument("Human input cannot own a child or effect");
        if(item.definition.kind==DynamicNodeKind::agent&&!item.human_request_id.empty())throw std::invalid_argument("Agent node cannot own a human input request");
        if(item.state==DynamicNodeState::settled){
            if(item.claim_revision==0||item.outcome_json.empty()||!item.settled_event_seq)throw std::invalid_argument("Settled dynamic node lacks its actual outcome");
            if(item.effect_state=="awaiting_approval"||item.effect_state=="ready"||item.effect_state=="executing")throw std::invalid_argument("Settled dynamic node still owns a nonterminal effect");
            if((item.definition.kind==DynamicNodeKind::agent||item.definition.kind==DynamicNodeKind::tool)&&(item.child_run_id.empty()||(item.child_state!="completed"&&item.child_state!="failed"&&item.child_state!="cancelled")))throw std::invalid_argument("Settled dynamic child lacks an actual terminal run");
            if(item.definition.kind==DynamicNodeKind::human&&item.human_request_id.empty())throw std::invalid_argument("Settled human node lacks its published request");
            if(item.definition.kind==DynamicNodeKind::human){const auto output=object(item.outcome_json,outcome_limit);const auto human_state=string(output,"human_state",16);
                if(human_state!="answered"&&human_state!="expired"&&human_state!="cancelled")throw std::invalid_argument("Settled human outcome is not an observed terminal request");
                if(output.contains("input_json")&&!output["input_json"].is_string())throw std::invalid_argument("Human input observation must remain an exact JSON data string");}
        }
        if(item.settled_event_seq&&(*item.settled_event_seq<1||*item.settled_event_seq>safe_integer))throw std::invalid_argument("Invalid dynamic settlement event");
        if(!item.outcome_json.empty())(void)object(item.outcome_json,outcome_limit);
        if(!item.effect_state.empty()&&item.effect_state!="none"&&item.effect_state!="awaiting_approval"&&item.effect_state!="ready"&&item.effect_state!="executing"&&item.effect_state!="succeeded"&&item.effect_state!="failed"&&item.effect_state!="cancelled"&&item.effect_state!="uncertain")throw std::invalid_argument("Invalid dynamic effect state");
    }
    if(published_humans!=value.humans_published||published_humans+pending_humans>value.capabilities.max_humans||retired.size()>value.nodes.size())throw std::invalid_argument("Dynamic human or retired-label allowance exceeded");
    if(pending_children!=value.planned_children_reserved||pending_humans!=value.planned_humans_reserved||pending_children>8)throw std::invalid_argument("Dynamic pending reservations differ from their immutable ledger");
    if(value.state=="completed"&&std::any_of(value.nodes.begin(),value.nodes.end(),[](const auto& item){return item.state!=DynamicNodeState::settled&&item.state!=DynamicNodeState::skipped&&item.state!=DynamicNodeState::cancelled;}))throw std::invalid_argument("Completed dynamic plan still owns unfinished definitions");
    for(const auto& item:retired)if(std::none_of(value.nodes.begin(),value.nodes.end(),[&](const auto& node){return node.definition.label==item;}))throw std::invalid_argument("Retired dynamic label has no immutable definition");
    topology(value.nodes);
}
bool observed(const DynamicNodeRecord& value){
    return value.state==DynamicNodeState::settled&&value.effect_state!="uncertain"&&value.effect_state!="executing"&&value.effect_state!="awaiting_approval"&&value.effect_state!="ready";
}
bool successful(const DynamicNodeRecord& value){
    if(!observed(value))return false;
    return value.definition.kind==DynamicNodeKind::human?object(value.outcome_json,outcome_limit).at("human_state")=="answered":value.child_state=="completed";
}
const DynamicNodeRecord& find(const DynamicPlanRecord& plan,const std::string& label){
    const auto found=std::find_if(plan.nodes.begin(),plan.nodes.end(),[&](const auto& value){return value.definition.label==label;});
    if(found==plan.nodes.end())throw NotFound("Dynamic task does not belong to this plan");return *found;
}
enum class Readiness {ready,waiting,blocked};
Readiness readiness(const DynamicPlanRecord& plan,const DynamicNodeRecord& item,std::map<std::string,Readiness>& cache){
    if(const auto found=cache.find(item.definition.label);found!=cache.end())return found->second;
    const auto remember=[&](Readiness value){cache.emplace(item.definition.label,value);return value;};
    bool waiting=false;
    for(const auto& edge:item.definition.dependencies){const auto& dependency=find(plan,edge.task);
        if(edge.require==DynamicDependencyRequirement::success?successful(dependency):observed(dependency))continue;
        if(observed(dependency)||dependency.state==DynamicNodeState::skipped||dependency.state==DynamicNodeState::cancelled||dependency.state==DynamicNodeState::uncertain||dependency.effect_state=="uncertain")return remember(Readiness::blocked);
        if((dependency.state==DynamicNodeState::pending||dependency.state==DynamicNodeState::blocked)&&readiness(plan,dependency,cache)==Readiness::blocked)return remember(Readiness::blocked);
        waiting=true;
    }return remember(waiting?Readiness::waiting:Readiness::ready);
}
Json public_node(const DynamicNodeRecord& item,bool include_outcome){
    Json result=definition_json(item.definition);result["definition_revision"]=item.definition_revision;result["state"]=state(item.state);result["protected"]=protected_node(item);
    if(item.definition.kind==DynamicNodeKind::agent)result["preset_revision"]=item.definition.preset_revision;
    if(item.claim_revision>0)result["claim_revision"]=item.claim_revision;
    if(!item.human_request_id.empty())result["human_request_id"]=item.human_request_id;
    if(!item.child_run_id.empty())result["child_run_id"]=item.child_run_id;
    if(!item.child_state.empty())result["child_state"]=item.child_state;
    if(!item.effect_state.empty())result["effect_state"]=item.effect_state;
    if(include_outcome&&!item.outcome_json.empty())result["outcome_json"]=item.outcome_json;
    return result;
}
}
DynamicPlanChange parse_dynamic_plan_change(const std::string& source,bool initial){
    const auto input=object(source,change_limit);
    if(initial)fields(input,{"expected_revision","expected_state_sequence","add"});else fields(input,{"expected_revision","expected_state_sequence","add","replace","skip"});
    DynamicPlanChange result;result.arguments_json=source;result.expected_revision=integer(input,"expected_revision",initial?0:1);result.expected_state_sequence=integer(input,"expected_state_sequence",initial?0:1);
    if(initial&&(result.expected_revision!=0||result.expected_state_sequence!=0))throw std::invalid_argument("Initial dynamic plan requires zero preconditions");
    std::set<std::string> labels;
    auto definitions=[&](const char* key,std::vector<DynamicNodeDefinition>& destination){
        if(!input.contains(key))return;if(!input[key].is_array()||input[key].size()>32)throw std::invalid_argument("Dynamic change requires bounded definition arrays");
        for(const auto& value:input[key]){auto task=node(value);if(!labels.insert(task.label).second)throw std::invalid_argument("A dynamic change repeats a task label");destination.push_back(std::move(task));}
    };definitions("add",result.add);definitions("replace",result.replace);
    if(input.contains("skip")){if(!input["skip"].is_array()||input["skip"].size()>32)throw std::invalid_argument("Dynamic skips require a bounded label array");
        for(const auto& value:input["skip"]){if(!value.is_string())throw std::invalid_argument("Dynamic skip requires a task label");auto id=value.get<std::string>();label(id);if(!labels.insert(id).second)throw std::invalid_argument("A dynamic change repeats a task label");result.skip.push_back(std::move(id));}}
    if(result.add.empty()&&result.replace.empty()&&result.skip.empty())throw std::invalid_argument("Dynamic plan change is empty");return result;
}
DynamicPlanCandidate reduce_dynamic_plan(const DynamicPlanRecord& plan,const DynamicPlanChange& change,const DynamicPlanCapabilities& frozen){
    capabilities(frozen);snapshot(plan,true);
    if(!frozen.catalogue_finalized)throw std::invalid_argument("Dynamic plan admission requires its finalized native catalogue");
    if(!same_capabilities(plan.capabilities,frozen))throw Conflict("Dynamic plan capabilities differ from their admitted snapshot");
    if(change.arguments_json.size()>static_cast<std::size_t>(frozen.change_bytes))throw std::invalid_argument("Dynamic change exceeds its admitted byte allowance");
    const auto parsed=parse_dynamic_plan_change(change.arguments_json,plan.revision==0);
    auto same_nodes=[](const auto& a,const auto& b){if(a.size()!=b.size())return false;for(std::size_t i=0;i<a.size();++i)if(!same_definition(a[i],b[i]))return false;return true;};
    if(parsed.expected_revision!=change.expected_revision||parsed.expected_state_sequence!=change.expected_state_sequence||!same_nodes(parsed.add,change.add)||!same_nodes(parsed.replace,change.replace)||parsed.skip!=change.skip)throw std::invalid_argument("Dynamic change differs from its exact model request");
    if(plan.revision!=change.expected_revision||plan.state_sequence!=change.expected_state_sequence)throw Conflict("Dynamic plan revision or observed state changed");
    if(plan.revision>=frozen.max_revisions||plan.state_sequence>=safe_integer)throw Conflict("Dynamic plan revision allowance exhausted");
    if(plan.state=="completed"||plan.state=="failed"||plan.state=="cancelled"||plan.state=="interrupted"||plan.state=="uncertain")throw Conflict("Retired dynamic plan cannot be revised");
    DynamicPlanCandidate result;result.nodes=plan.nodes;result.retired_labels=plan.retired_labels;
    auto mutable_node=[&](const std::string& id)->DynamicNodeRecord&{
        auto found=std::find_if(result.nodes.begin(),result.nodes.end(),[&](const auto& value){return value.definition.label==id;});
        if(found==result.nodes.end())throw std::invalid_argument("Dynamic revision names an unknown task");
        if(protected_node(*found))throw Conflict("Claimed, published, settled or retired dynamic work is immutable");return *found;
    };
    for(const auto& replacement:change.replace){definition(replacement,frozen);auto& original=mutable_node(replacement.label);
        if(same_definition(original.definition,replacement))throw std::invalid_argument("Dynamic replacement does not change its definition");
        if(original.definition.kind!=DynamicNodeKind::human&&replacement.kind==DynamicNodeKind::human)++result.newhuman_count;
        original.definition=replacement;original.definition_revision=plan.revision+1;
    }
    for(const auto& id:change.skip){auto& original=mutable_node(id);original.state=DynamicNodeState::skipped;result.retired_labels.push_back(id);}
    for(const auto& addition:change.add){definition(addition,frozen);
        if(std::any_of(result.nodes.begin(),result.nodes.end(),[&](const auto& value){return value.definition.label==addition.label;})||std::find(result.retired_labels.begin(),result.retired_labels.end(),addition.label)!=result.retired_labels.end())throw Conflict("Dynamic task label already exists or is permanently retired");
        DynamicNodeRecord record;record.definition=addition;record.state=DynamicNodeState::pending;record.definition_revision=plan.revision+1;result.nodes.push_back(std::move(record));
        if(addition.kind==DynamicNodeKind::human)++result.newhuman_count;
    }
    if(result.nodes.size()>static_cast<std::size_t>(frozen.max_nodes))throw std::invalid_argument("Dynamic lifetime task-label allowance exceeded");
    auto specification=Json{{"nodes",Json::array()}};
    for(const auto& value:result.nodes){if(value.definition.kind==DynamicNodeKind::human&&(value.state==DynamicNodeState::pending||value.state==DynamicNodeState::blocked))++result.planned_humans_reserved;
        if(value.definition.kind!=DynamicNodeKind::human&&(value.state==DynamicNodeState::pending||value.state==DynamicNodeState::blocked))++result.planned_children_reserved;
        specification["nodes"].push_back(definition_json(value.definition));}
    if(plan.humans_published+result.planned_humans_reserved>frozen.max_humans||result.planned_children_reserved>8)throw std::invalid_argument("Dynamic plan cannot preserve its human or executable allowances");
    topology(result.nodes);result.canonical_spec_json=specification.dump();
    if(result.canonical_spec_json.size()>change_limit)throw std::invalid_argument("Dynamic complete definition exceeds its byte limit");return result;
}
DynamicPlanCoordinator::DynamicPlanCoordinator(DynamicPlanRecord value):observed_(std::move(value)){snapshot(observed_);}
DynamicPlanDecision DynamicPlanCoordinator::inspect()const{
    DynamicPlanDecision result;bool pending=false;std::map<std::string,Readiness> cache;
    result.halted=observed_.state=="interrupted"||observed_.state=="uncertain"||observed_.state=="failed"||observed_.state=="cancelled";
    for(const auto& item:observed_.nodes){
        if(item.state==DynamicNodeState::uncertain||item.effect_state=="uncertain")result.halted=true;
        if(item.state==DynamicNodeState::claimed)result.claimed.push_back(item.definition.label);
        else if(item.state==DynamicNodeState::waiting_human)result.waiting_human.push_back(item.definition.label);
        else if(item.state==DynamicNodeState::pending||item.state==DynamicNodeState::blocked){pending=true;const auto eligible=readiness(observed_,item,cache);
            if(eligible==Readiness::ready)result.ready.push_back(item.definition.label);else if(eligible==Readiness::blocked)result.blocked.push_back(item.definition.label);}
    }
    if(result.halted)result.ready.clear();
    const bool idle=result.claimed.empty()&&result.waiting_human.empty();result.finished=!pending&&idle&&!result.halted;
    result.report_ready=idle&&result.ready.empty();return result;
}
DynamicPreparedNode DynamicPlanCoordinator::prepare(const std::string& id)const{
    const auto decision=inspect();if(decision.halted||std::find(decision.ready.begin(),decision.ready.end(),id)==decision.ready.end())throw Conflict("Dynamic task has no executable owned frontier");
    const auto& item=find(observed_,id);Json observations=Json::object();
    for(const auto& edge:item.definition.dependencies){const auto& dependency=find(observed_,edge.task);
        Json output={{"require",requirement(edge.require)},{"state",state(dependency.state)},{"outcome_json",dependency.outcome_json}};
        if(!dependency.child_state.empty())output["child_state"]=dependency.child_state;
        if(!dependency.effect_state.empty())output["effect_state"]=dependency.effect_state;
        observations[edge.task]=std::move(output);
    }
    auto source=observations.dump();if(source.size()>report_limit)throw std::invalid_argument("Dynamic dependency observations exceed their byte limit");return {item,std::move(source)};
}
std::string DynamicPlanCoordinator::report()const{
    const auto decision=inspect();Json result={{"source","native_dynamic_plan"},{"plan_id",observed_.id},{"root_run_id",observed_.root_run_id},{"revision",observed_.revision},{"state_sequence",observed_.state_sequence},{"finished",decision.finished},{"halted",decision.halted},{"ready",decision.ready},{"blocked",decision.blocked},{"waiting_human",decision.waiting_human},{"nodes",Json::array()}};
    for(const auto& item:observed_.nodes){auto output=public_node(item,true);if(!item.child_run_id.empty())output["history_ref"]="/v1/runs/"+observed_.root_run_id+"/children/"+item.child_run_id+"/history";result["nodes"].push_back(std::move(output));}
    auto source=result.dump();if(source.size()<=report_limit)return source;
    result["error"]={{"code","result_limit_exceeded"}};result["nodes"]=Json::array();
    for(const auto& item:observed_.nodes){Json output={{"id",item.definition.label},{"type",kind(item.definition.kind)},{"state",state(item.state)},{"definition_revision",item.definition_revision},{"protected",protected_node(item)}};
        if(!item.human_request_id.empty())output["human_request_id"]=item.human_request_id;
        if(!item.child_run_id.empty()){output["child_run_id"]=item.child_run_id;output["child_state"]=item.child_state;output["history_ref"]="/v1/runs/"+observed_.root_run_id+"/children/"+item.child_run_id+"/history";}result["nodes"].push_back(std::move(output));}
    source=result.dump();if(source.size()>report_limit)throw std::invalid_argument("Dynamic plan identity report exceeds its byte limit");return source;
}
DynamicPlanDecision inspect_dynamic_plan(const DynamicPlanRecord& value){return DynamicPlanCoordinator(value).inspect();}
DynamicPreparedNode prepare_dynamic_node(const DynamicPlanRecord& value,const std::string& id){return DynamicPlanCoordinator(value).prepare(id);}
std::string dynamic_plan_report(const DynamicPlanRecord& value){return DynamicPlanCoordinator(value).report();}
std::vector<ModelToolDefinition> dynamic_plan_tool_definitions(const DynamicPlanCapabilities& frozen){
    if(!frozen.catalogue_finalized)throw std::invalid_argument("Planning tools require their finalized native catalogue");
    return context_dynamic_plan_tool_definitions(frozen);
}
std::vector<ModelToolDefinition> context_dynamic_plan_tool_definitions(const DynamicPlanCapabilities& frozen){
    capabilities(frozen);Json presets=Json::array();for(const auto& preset:frozen.presets)presets.push_back(preset.id);
    const auto dependency=Json{{"type","object"},{"properties",{{"task",{{"type","string"},{"maxLength",32}}},{"require",{{"enum",{"success","observed"}}}}}},{"required",{"task","require"}},{"additionalProperties",false}};
    const auto common=Json{{"id",{{"type","string"},{"minLength",1},{"maxLength",32},{"pattern","^[A-Za-z0-9_.-]+$"}}},{"type",{{"const","agent"}}},{"objective",{{"type","string"},{"minLength",1},{"maxLength",8192}}},{"preset",{{"enum",presets}}},{"depends_on",{{"type","array"},{"maxItems",32},{"items",dependency}}}};
    const auto agent=Json{{"type","object"},{"properties",common},{"required",{"id","type","objective","preset","depends_on"}},{"additionalProperties",false}};
    Json read_tools=Json::array();for(const auto* name:{"read_repository_instructions","read_file","list_files","search_files","find_files","glob_files"})if(std::any_of(frozen.presets.begin(),frozen.presets.end(),[&](const auto& preset){return preset.id=="workspace.inspect"&&preset.readonly&&std::find(preset.tools.begin(),preset.tools.end(),name)!=preset.tools.end();}))read_tools.push_back(name);
    const auto catalog=object(frozen.tool_catalog_json,change_limit);
    if(catalog.is_object()&&catalog.contains("tools")&&catalog["tools"].is_array())for(const auto& policy:frozen.presets)if(policy.id=="workspace.coding"&&!policy.readonly)for(const auto& name:policy.tools)if(name.starts_with("mcp_")){
        const auto found=std::find_if(catalog["tools"].begin(),catalog["tools"].end(),[&](const auto& item){return item.is_object()&&item.value("name",std::string{})==name;});
        if(found!=catalog["tools"].end()&&found->contains("mcp_binding")&&found->at("mcp_binding").is_object()&&
            (!found->at("mcp_binding").contains("kind")||found->at("mcp_binding").value("kind",std::string{})=="resource"||found->at("mcp_binding").value("kind",std::string{})=="prompt"))read_tools.push_back(name);
    }
    if(catalog.is_object()&&catalog.contains("tools")&&catalog["tools"].is_array())for(const auto& policy:frozen.presets)if(policy.id=="workspace.coding"&&!policy.readonly)for(const auto& name:policy.tools)if(name.starts_with("a2a_delegate_")){
        const auto found=std::find_if(catalog["tools"].begin(),catalog["tools"].end(),[&](const auto& item){return item.is_object()&&item.value("name",std::string{})==name;});
        if(found!=catalog["tools"].end()&&found->contains("a2a_binding")&&found->at("a2a_binding").is_object()&&found->at("a2a_binding").value("kind",std::string{})=="a2a_remote_agent")read_tools.push_back(name);
    }
    if(catalog.is_object()&&catalog.contains("tools")&&catalog["tools"].is_array())for(const auto& policy:frozen.presets)if(policy.id=="workspace.coding"&&!policy.readonly&&std::find(policy.tools.begin(),policy.tools.end(),"run_process")!=policy.tools.end()){
        const auto found=std::find_if(catalog["tools"].begin(),catalog["tools"].end(),[](const auto& item){return item.is_object()&&item.value("name",std::string{})=="run_process";});
        if(found!=catalog["tools"].end()&&!found->contains("mcp_binding"))read_tools.push_back("run_process");
    }
    auto human_properties=common;human_properties.erase("objective");human_properties.erase("preset");human_properties["type"]={{"const","human"}};human_properties["question"]={{"type","string"},{"minLength",1},{"maxLength",8192}};
    const auto human=Json{{"type","object"},{"properties",human_properties},{"required",{"id","type","question","depends_on"}},{"additionalProperties",false}};
    auto choices=Json::array({agent});
    if(!read_tools.empty()){
        auto properties=common;properties["type"]={{"const","tool"}};properties.erase("objective");properties.erase("preset");properties["tool"]={{"enum",read_tools}};properties["arguments_json"]={{"type","string"},{"minLength",2},{"maxLength",65536}};
        choices.push_back({{"type","object"},{"properties",properties},{"required",{"id","type","tool","arguments_json","depends_on"}},{"additionalProperties",false}});
    }
    choices.push_back(human);const auto definitions=Json{{"type","array"},{"maxItems",32},{"items",{{"oneOf",choices}}}};
    Json initial={{"type","object"},{"properties",{{"expected_revision",{{"type","integer"},{"const",0}}},{"expected_state_sequence",{{"type","integer"},{"const",0}}},{"add",definitions}}},{"required",{"expected_revision","expected_state_sequence","add"}},{"additionalProperties",false}};initial["properties"]["add"]["minItems"]=1;
    auto revised=initial;revised["required"]={"expected_revision","expected_state_sequence"};
    for(const auto* field:{"expected_revision","expected_state_sequence"})revised["properties"][field]={{"type","integer"},{"minimum",1},{"maximum",safe_integer}};
    revised["properties"]["add"]=definitions;revised["properties"]["replace"]=definitions;
    revised["properties"]["skip"]={{"type","array"},{"maxItems",32},{"items",{{"type","string"},{"minLength",1},{"maxLength",32},{"pattern","^[A-Za-z0-9_.-]+$"}}}};
    return {{"plan_tasks","Select a native dependency plan for this ordinary Agent. Use agent tasks for model reasoning, read-only tool tasks for deterministic workspace reads and registered MCP resource/prompt reads, MCP and registered A2A tool tasks for external actions, run_process tool tasks for synchronous commands through registered profiles, and human tasks for authenticated input. MCP resource and prompt reads still require their registered controller approval and journal; effects keep their usual exact approval. Human answers are data. This must be the sole tool call in the assistant turn.",initial.dump()},
        {"revise_plan","Revise the same native plan using its observed revision and state sequence. Only never-claimed pending or blocked tasks can change; completed, failed, published and effect-linked work remains immutable. MCP, A2A and process tool tasks keep their frozen bindings and require the usual controller approval. This must be the sole tool call in the assistant turn.",revised.dump()},
        {"inspect_plan","Read this Agent's actual owned dependency plan, revisions and bounded outcomes. This does not dispatch work or grant effect authority.",R"({"type":"object","properties":{},"additionalProperties":false})"}};
}
}
