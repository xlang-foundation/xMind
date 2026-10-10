#include "agentflow/mcp_client_factory.hpp"
#include "agentflow/agent_runner.hpp"
#include "nlohmann/json.hpp"
#include "agentflow/edit_executor.hpp"
#include "agentflow/mcp_tool_registry.hpp"
#include "agentflow/create_executor.hpp"
#include "agentflow/patch_tool.hpp"
#include "agentflow/schema_worker.hpp"
#include "agentflow/repository_instruction_context.hpp"
#include "agentflow/mcp_wire.hpp"
#include "agentflow/delegation_executor.hpp"
#include "agentflow/dynamic_plan_executor.hpp"
#include "agentflow/agent_authority.hpp"
#include "agentflow/run_executor.hpp"
#define NOMINMAX
#include <windows.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <map>
#include <set>
#include <filesystem>

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr std::string_view instruction_prefix="\n\nBackend-configured agent instructions (native permissions and execution evidence remain authoritative):\n";
std::string operation_id() {std::random_device random;std::ostringstream value;value<<std::hex<<std::setfill('0');for(int i=0;i<4;++i) value<<std::setw(8)<<random();return value.str();}
std::string public_context_usage(const std::string& raw){
    const auto actual=Json::parse(raw);if(actual.is_null())return "null";
    if(!actual.is_object())throw ModelProtocolError("Stored compaction usage has no valid public shape");
    auto result=Json::object();
    for(const auto* key:{"input_tokens","output_tokens","total_tokens"})if(actual.contains(key))result[key]=actual.at(key);
    for(const auto* key:{"input_tokens_details","output_tokens_details"})if(actual.contains(key)){
        auto details=Json::object();const auto& value=actual.at(key);
        if(value.is_null()){result[key]=nullptr;continue;}
        if(!value.is_object())throw ModelProtocolError("Stored compaction usage details are invalid");
        for(const auto* name:{"cached_tokens","reasoning_tokens"})if(value.contains(name))details[name]=value.at(name);
        result[key]=std::move(details);
    }
    return result.dump();
}
void cancelled(std::stop_token token) {if(token.stop_requested()) throw TransportCancelled("Agent cancelled");}
MessageRole role(const std::string& name) {
    if(name=="user") return MessageRole::user;if(name=="assistant") return MessageRole::assistant;
    if(name=="tool") return MessageRole::tool;if(name=="system") return MessageRole::system;if(name=="developer") return MessageRole::developer;
    throw ModelProtocolError("Unsupported stored conversation role");
}
ModelMessage message(const Message& stored) {
    const auto data=Json::parse(stored.json);
    if(!data.is_object() || !data.contains("content") || !data["content"].is_string()) throw ModelProtocolError("Unsupported stored conversation content");
    ModelMessage result{role(stored.role),data["content"].get<std::string>()};
    if(data.contains("refusal")) result.refusal=data["refusal"].get<std::string>();
    if(data.contains("provider_items"))result.provider_items_json=data["provider_items"].dump();
    if(data.contains("tool_call_id")) result.tool_call_id=data["tool_call_id"].get<std::string>();
    if(data.contains("tool_calls")) for(const auto& call:data["tool_calls"]) result.tool_calls.push_back({call.at("id").get<std::string>(),call.at("name").get<std::string>(),call.at("arguments").get<std::string>()});
    return result;
}
Json assistant(const ModelCompletion& result,const std::string& model,std::int64_t elapsed_ms,std::optional<std::int64_t> first_token_ms) {
    Json value={{"content",result.content},{"model",model},{"elapsed_ms",elapsed_ms}};
    if(result.usage_json!="null") value["usage"]=Json::parse(result.usage_json);
    if(first_token_ms) value["first_token_ms"]=*first_token_ms;
    if(!result.refusal.empty()) value["refusal"]=result.refusal;
    if(result.provider_items_json!="[]")value["provider_items"]=Json::parse(result.provider_items_json);
    if(!result.tool_calls.empty()) {
        value["tool_calls"]=Json::array();
        for(const auto& call:result.tool_calls) value["tool_calls"].push_back({{"id",call.id},{"name",call.name},{"arguments",call.arguments_json}});
    }
    return value;
}
std::string execution_identity(const AgentSettings& settings,const std::string& model){
    auto context=provider_context_json(settings,model);if(!context.empty())return context;
    const char* wire=settings.provider.wire==ProviderWire::responses?"responses":settings.provider.wire==ProviderWire::anthropic_messages?"anthropic-messages":settings.provider.wire==ProviderWire::gemini_generate_content?"gemini-generate-content":"chat-completions";
    auto identity=Json{{"wire",wire},{"model_id",model.empty()?settings.provider.model:model}};
    if(settings.provider.chat_dialect==ChatDialect::deepseek)identity["chat_dialect"]="deepseek";
    else if(settings.provider.chat_dialect!=ChatDialect::openai)throw std::invalid_argument("Invalid native chat dialect");
    return identity.dump();
}
std::string tool_catalogue(const std::vector<ModelToolDefinition>& definitions,const Json& actual_bindings){
    if(!actual_bindings.is_array())throw DynamicPlanUnavailable("Actual MCP approval bindings are not a native array");
    std::map<std::string,Json> bindings;
    for(const auto& binding:actual_bindings){
        if(!binding.is_object()||!binding.contains("alias")||!binding["alias"].is_string()||
            !bindings.emplace(binding["alias"].get<std::string>(),binding).second)
            throw DynamicPlanUnavailable("Actual MCP approval bindings contain an invalid or duplicate alias");
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
            if(!definition.name.starts_with("mcp_")||!binding->second.contains("input_schema_json")||
                binding->second["input_schema_json"]!=definition.input_schema_json)
                throw DynamicPlanUnavailable("Actual MCP approval binding differs from its native tool schema");
            captured["mcp_binding"]=binding->second;++matched_bindings;
        }else if(definition.name.starts_with("mcp_"))throw DynamicPlanUnavailable("Actual MCP alias has no native approval binding");
        result["tools"].push_back(std::move(captured));
    }
    if(matched_bindings!=bindings.size())throw DynamicPlanUnavailable("Actual MCP approval binding has no discovered native tool");
    return result.dump();
}
void verify_child_catalogue(const std::vector<ModelToolDefinition>& actual,const Json& bindings,const DynamicPlanCapabilities& caps,const DynamicPresetCapability& preset){
    const auto sealed=Json::parse(caps.tool_catalog_json);Json expected={{"tools",Json::array()}};
    const std::set<std::string> allowed(preset.tools.begin(),preset.tools.end());
    for(const auto& definition:sealed.at("tools"))if(allowed.contains(definition.at("name").get<std::string>()))expected["tools"].push_back(definition);
    if(expected["tools"].size()!=allowed.size()||Json::parse(tool_catalogue(actual,bindings))!=expected)
        throw DynamicPlanUnavailable("Actual child catalogue differs from its sealed native preset");
}
}
AgentRunner::AgentRunner(PersistenceService& persistence,AgentSettings settings,std::shared_ptr<DelegationExecutor> delegation,
    std::shared_ptr<DynamicPlanExecutor> planning):persistence_(persistence),settings_(std::move(settings)),delegation_(std::move(delegation)),planning_(std::move(planning)) {
    if(settings_.instruction_policy.instructions.size()>32768 || settings_.instruction_policy.instructions.find('\0')!=std::string::npos || settings_.instruction_policy.revision<0 || settings_.instruction_policy.revision>9007199254740991 || (!settings_.instruction_policy.instructions.empty() && settings_.instruction_policy.revision==0))throw std::invalid_argument("Invalid backend instruction policy");
    if(!settings_.instruction_policy.instructions.empty() && settings_.instructions.size()+settings_.instruction_policy.instructions.size()+instruction_prefix.size()>65536)throw std::invalid_argument("Combined agent instructions exceed limits");
    if(settings_.provider.model.empty() || settings_.provider.endpoint.empty() || settings_.max_turns==0 || settings_.max_turns>128 || settings_.instructions.size()>65536 || settings_.run_timeout.count()<=0 || settings_.run_timeout.count()>3600000)
        throw std::invalid_argument("Invalid agent configuration");
    if(settings_.workspace) {
        if(settings_.provider.tools!=Capability::supported) throw std::invalid_argument("Workspace agent requires declared model tool capability");
        workspace_=std::make_unique<WorkspaceTools>(*settings_.workspace);
    }
    if(settings_.approved_edits && !workspace_) throw std::invalid_argument("Approved edits require a workspace");
    if(!settings_.process_profiles.empty()) {
        if(!workspace_)throw std::invalid_argument("Process profiles require a verified workspace");
        process_=std::make_unique<ProcessExecutor>(persistence_,*workspace_,*settings_.workspace,settings_.process_profiles);
        settings_.process_profiles=process_->profiles();
    }
    if(settings_.mcp_servers.size()>16)throw std::invalid_argument("MCP server count exceeds limits");
    std::set<std::string> mcp_ids;
    for(const auto& server:settings_.mcp_servers){if(server.id.empty() || server.revision<=0 || !mcp_ids.insert(server.id).second)throw std::invalid_argument("Invalid registered MCP configuration");if(server.enabled && !workspace_)throw std::invalid_argument("MCP execution requires a verified workspace and model tool capability");}
    if(settings_.selectable_models.size()>64) throw std::invalid_argument("Model catalogue exceeds limits");
    for(const auto& model:models()) if(model.empty() || model.size()>256 || model.find('\0')!=std::string::npos) throw std::invalid_argument("Invalid configured model ID");
    if(settings_.delegation){const auto& policy=*settings_.delegation;if(!workspace_||policy.preset_id!="workspace.inspect"||policy.preset_revision!=1||policy.max_children<1||policy.max_children>8||policy.max_parallel<1||policy.max_parallel>2||policy.max_model_calls<1||policy.max_model_calls>32||policy.max_leaf_turns<1||policy.max_leaf_turns>4)throw std::invalid_argument("Invalid registered agent delegation policy");}
    if(settings_.planning){auto& policy=*settings_.planning;
        if(!workspace_||!planning_||settings_.max_turns>16||policy.max_nodes<1||policy.max_nodes>32||policy.max_revisions<1||policy.max_revisions>16||
            policy.max_humans<1||policy.max_humans>8||policy.change_bytes<1||policy.change_bytes>262144||
            policy.human_expiry_ms<1||policy.human_expiry_ms>900000)throw std::invalid_argument("Invalid registered dynamic planning policy");
        // This allowance is inherited from the native agent configuration.
        policy.max_parent_turns=static_cast<std::int64_t>(settings_.max_turns);
    }
    if(settings_.context){
        if(settings_.max_turns>16)throw std::invalid_argument("Context-enabled ordinary agents require bounded logical turns");
        ContextManager validate(persistence_,settings_.provider,*settings_.context,message);
    }
}
AgentRunner::~AgentRunner()=default;
std::vector<std::string> AgentRunner::models() const {
    std::vector<std::string> result{settings_.provider.model};
    for(const auto& model:settings_.selectable_models) if(std::find(result.begin(),result.end(),model)==result.end()) result.push_back(model);
    return result;
}
std::string provider_context_json(const AgentSettings& settings,const std::string& model_id){
    if(!settings.provider_identity)return {};
    const auto& identity=*settings.provider_identity;
    const auto wire=settings.provider.wire==ProviderWire::responses?"responses":settings.provider.wire==ProviderWire::anthropic_messages?"anthropic-messages":settings.provider.wire==ProviderWire::gemini_generate_content?"gemini-generate-content":"chat-completions";
    return nlohmann::json{{"profile_id",identity.profile_id},{"profile_revision",identity.profile_revision},{"route_id",identity.route_id},{"provider",identity.provider},{"wire",wire},{"model_id",model_id.empty()?settings.provider.model:model_id}}.dump();
}
std::optional<RootBudgetSpec> AgentRunner::execution_budget(const std::string& model_id)const{
    if((!settings_.delegation||!delegation_)&&(!settings_.planning||!planning_)){
        if(!settings_.context)return {};
        return RootBudgetSpec{"native.context",1,workspace_?workspace_->identity():"workspace:none",
            execution_identity(settings_,model_id),0,0,32,settings_.run_timeout.count()};
    }
    const auto policy=settings_.delegation.value_or(AgentDelegationPolicy{});const auto context=execution_identity(settings_,model_id);
    return RootBudgetSpec{settings_.planning?"native.dynamic-plan":"native.delegation",1,workspace_->identity(),context,static_cast<std::int64_t>(policy.max_children),static_cast<std::int64_t>(policy.max_parallel),static_cast<std::int64_t>(policy.max_model_calls),settings_.run_timeout.count()};
}
std::optional<DynamicPlanCapabilities> AgentRunner::execution_capabilities(const std::string& model_id)const{
    if(!settings_.planning||!planning_)return {};
    const auto& model=model_id.empty()?settings_.provider.model:model_id;
    DynamicPlanCapabilities caps;static_cast<DynamicPlanningPolicy&>(caps)=*settings_.planning;
    caps.tool_catalog_json="{}";
    caps.workspace_identity=workspace_->identity();caps.provider_identity_json=execution_identity(settings_,model);
    caps.backend_identity=agent_authority_identity(settings_,model,caps.workspace_identity,
        agent_authority_credentials(persistence_,settings_));
    DynamicPresetCapability inspect;inspect.id="workspace.inspect";
    for(const auto& definition:workspace_->definitions())inspect.tools.push_back(definition.name);
    for(const auto& definition:SkillContext::definitions())inspect.tools.push_back(definition.name);
    caps.presets.push_back(inspect);
    if(settings_.approved_edits||process_||std::any_of(settings_.mcp_servers.begin(),settings_.mcp_servers.end(),[](const auto& server){return server.enabled;})){
        auto coding=inspect;coding.id="workspace.coding";coding.readonly=false;
        if(settings_.approved_edits){coding.tools.push_back("edit_file");coding.tools.push_back("create_file");coding.tools.push_back("apply_patch");}
        if(process_)coding.tools.push_back("run_process");caps.presets.push_back(std::move(coding));
    }
    return caps;
}
std::string AgentRunner::admitted_dynamic_model(const std::string& root_id)const{
    if(!settings_.planning||!planning_)throw DynamicPlanUnavailable("Native dynamic planning is unavailable");
    const auto saved=persistence_.root_budget(root_id).get();
    const auto identity=Json::parse(saved.spec.provider_identity_json);
    if(!identity.contains("model_id")||!identity["model_id"].is_string())throw DynamicPlanUnavailable("Dynamic owner has no selected model identity");
    const auto model=identity["model_id"].get<std::string>();const auto configured=models();
    if(std::find(configured.begin(),configured.end(),model)==configured.end())throw DynamicPlanUnavailable("Admitted dynamic model is no longer configured");
    return model;
}
void AgentRunner::validate_dynamic_owner(const std::string& root_id,const std::string& model_id)const{
    const auto root=persistence_.run(root_id).get();
    if(root.graph_root||!root.parent_id.empty()||!settings_.planning||!planning_)throw DynamicPlanUnavailable("Dynamic execution requires its ordinary root owner");
    const auto model=model_id.empty()?admitted_dynamic_model(root_id):model_id;
    std::optional<DynamicPlanCapabilities> current;
    try{current=execution_capabilities(model);}
    catch(const std::invalid_argument&){throw DynamicPlanUnavailable("Admitted dynamic credential metadata is unavailable");}
    const auto saved=persistence_.dynamic_capabilities(root_id).get();
    const auto budget=persistence_.root_budget(root_id).get();const auto expected=execution_budget(model);
    if(!current||!expected||saved.backend_identity!=current->backend_identity||saved.workspace_identity!=current->workspace_identity||
        saved.provider_identity_json!=current->provider_identity_json||budget.spec.policy_id!=expected->policy_id||
        budget.spec.policy_revision!=expected->policy_revision||budget.spec.max_children!=expected->max_children||
        budget.spec.max_parallel!=expected->max_parallel||budget.spec.max_model_calls!=expected->max_model_calls||
        budget.spec.wall_limit_ms!=expected->wall_limit_ms||budget.spec.workspace_identity!=saved.workspace_identity||
        budget.spec.provider_identity_json!=saved.provider_identity_json)
        throw DynamicPlanUnavailable("Admitted dynamic settings or credential metadata changed");
    try{for(const auto& profile:settings_.process_profiles){
        if(profile.executable_id.empty()||ForegroundProcess::executable_identity(profile.executable)!=profile.executable_id)
            throw DynamicPlanUnavailable("Admitted dynamic process binding changed");
    }}catch(const ProcessBeforeDispatchError&){throw DynamicPlanUnavailable("Admitted dynamic process binding is unavailable");}
      catch(const std::filesystem::filesystem_error&){throw DynamicPlanUnavailable("Admitted dynamic process binding is unavailable");}
}
Run AgentRunner::start(std::string id,std::string session_id,std::string prompt,const std::string& model_id,const std::optional<AgentDefinition>& selected_agent) {
    if(prompt.empty() || prompt.size()>1024*1024) throw std::invalid_argument("Prompt must contain 1-1048576 UTF-8 bytes");
    auto input=Json{{"content",std::move(prompt)}};const auto context=provider_context_json(settings_,model_id);if(!context.empty())input["provider_context"]=Json::parse(context);
    if(selected_agent){const auto& agent=*selected_agent;if(agent.id.empty()||agent.id.size()>64||agent.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos||agent.revision<1||agent.revision>9007199254740991LL||agent.instructions.empty()||agent.instructions.size()>32768||agent.instructions.find('\0')!=std::string::npos||agent.model_id.size()>256||agent.model_id.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid selected agent snapshot");input["agent_profile"]={{"id",agent.id},{"revision",agent.revision},{"model_id",agent.model_id}};}
    return persistence_.start_prompt_run(std::move(id),std::move(session_id),input.dump(),execution_budget(model_id),execution_capabilities(model_id)).get();
}
ContextBinding AgentRunner::context_binding(const std::string& model_id)const{
    if(!settings_.context)throw RunUnavailable("This provider has no registered context strategy");
    const auto& selected=model_id.empty()?settings_.provider.model:model_id;
    const auto allowed=models();if(std::find(allowed.begin(),allowed.end(),selected)==allowed.end())throw std::invalid_argument("Context model is not configured");
    const auto& policy=settings_.context->compaction;
    return {execution_identity(settings_,selected),context_authority_identity(settings_,selected,
        workspace_?workspace_->identity():"workspace:none",agent_authority_credentials(persistence_,settings_)),
        policy.id,policy.revision,policy.strategy_id,policy.strategy_revision};
}
ContextControlSnapshot AgentRunner::context_status(const std::string& session,const std::string& model)const{
    persistence_.session(session).get();ContextControlSnapshot result;result.session_id=session;
    result.model_id=model.empty()?settings_.provider.model:model;result.enabled=settings_.context.has_value();
    if(!result.enabled)return result;
    const auto binding=context_binding(result.model_id);const ContextScope scope{ContextScopeKind::session,session};
    const auto observation=persistence_.context_status_observation(scope,binding).get();
    result.automatic=settings_.context->compaction.automatic;result.head_revision=observation.head.revision;
    result.source_watermark=observation.source_watermark;
    if(const auto& manual=observation.current_manual)
        result.manual=ContextManualStatus{manual->id,manual->state};
    if(const auto& checkpoint=observation.checkpoint)
        result.checkpoint=ContextCompactionStatus{checkpoint->compaction_id,checkpoint->measured_elapsed_ms,
            checkpoint->measured_preparation_elapsed_ms,public_context_usage(checkpoint->actual_usage_json)};
    return result;
}
ContextProjection AgentRunner::compact_idle_context(const IdleContextOwnerRecord& owner,
    std::chrono::steady_clock::time_point deadline,const std::string& model,std::stop_token token){
    const auto binding=context_binding(model);const auto before_read=std::chrono::steady_clock::now();
    const auto current=persistence_.idle_context_owner(owner.spec.id).get();
    if(current.state!="active"||current.spec.scope.kind!=ContextScopeKind::session||
       current.spec.binding!=binding||current.spec.manual_request_id!=owner.spec.manual_request_id||
       current.spec.scope!=owner.spec.scope||current.remaining_active_ms<1)
        throw ContextBindingChanged("Idle context preparation has no exact active owner");
    deadline=std::min(deadline,before_read+std::chrono::milliseconds(current.remaining_active_ms));
    const auto check=[&]{cancelled(token);if(std::chrono::steady_clock::now()>=deadline)throw RootBudgetDeadlineExceeded("Idle context preparation deadline elapsed");};
    check();auto provider=settings_.provider;if(!model.empty())provider.model=model;
    ModelRequest trusted;trusted.include_usage=provider.stream_usage==Capability::supported;trusted.max_output_tokens=settings_.max_output_tokens;
    auto instructions=settings_.instructions;
    if(workspace_){RepositoryInstructionContext context(*workspace_,workspace_->repository_instructions(".",token));instructions+=context.prepare(token);}
    if(!settings_.instruction_policy.instructions.empty()){instructions.append(instruction_prefix);instructions+=settings_.instruction_policy.instructions;}
    if(instructions.size()>65536)throw ModelRequestCapacityExceeded("Current idle context instructions exceed limits");
    if(!instructions.empty())trusted.messages.push_back({MessageRole::system,std::move(instructions)});
    if(workspace_){trusted.tools=workspace_->definitions();for(auto& definition:SkillContext::definitions())trusted.tools.push_back(std::move(definition));}
    if(settings_.approved_edits){trusted.tools.push_back(EditExecutor::definition());trusted.tools.push_back(CreateExecutor::definition());trusted.tools.push_back(PatchTool::definition());}
    if(process_)trusted.tools.push_back(process_->definition());
    struct McpRuntime {std::unique_ptr<McpToolClient> client;std::unique_ptr<McpToolRegistry> registry;};
    std::vector<McpRuntime> peers;std::set<std::string> aliases;for(const auto& definition:trusted.tools)aliases.insert(definition.name);
    for(const auto& server:settings_.mcp_servers){
        if(!server.enabled)continue;check();
        check();McpRuntime runtime;runtime.client=connect_mcp_client(server,persistence_,deadline,token);
        runtime.registry=std::make_unique<McpToolRegistry>(*runtime.client,persistence_,*workspace_,server.id,server.revision,deadline,token);
        for(const auto& definition:runtime.registry->definitions()){
            if(trusted.tools.size()>=64||!aliases.insert(definition.name).second)throw ModelRequestCapacityExceeded("Idle model tool catalogue exceeds limits or has an alias collision");
            trusted.tools.push_back(definition);
        }
        peers.push_back(std::move(runtime));
    }
    if(delegation_&&settings_.delegation)trusted.tools.push_back(DelegationExecutor::definition());
    if(const auto planning=execution_capabilities(provider.model))for(auto& definition:context_dynamic_plan_tool_definitions(*planning))trusted.tools.push_back(std::move(definition));
    if(trusted.tools.size()>64)throw ModelRequestCapacityExceeded("Idle model tool catalogue exceeds limits");
    check();std::optional<SecretBytes> credential;
    if(settings_.credential)credential=persistence_.resolve_credential(settings_.credential->scope,settings_.credential->id,settings_.credential->purpose).get();
    if(context_binding(provider.model)!=binding)throw ContextBindingChanged("Idle context authority changed during preparation");
    check();ContextManager manager(persistence_,provider,*settings_.context,message);
    return manager.compact_idle(current.spec.scope,binding,current.spec.id,trusted,credential?&*credential:nullptr,token,deadline);
}
Run AgentRunner::execute(const std::string& id,std::stop_token token,const std::string& model_id,
    std::shared_ptr<RootExecutionBudget> budget,const std::string& graph_instructions) {
    if(graph_instructions.size()>32768 || graph_instructions.find('\0')!=std::string::npos)throw std::invalid_argument("Graph agent instructions exceed limits");
    const auto admitted=persistence_.run(id).get();if(admitted.graph_root)throw std::invalid_argument("Graph roots require their owning graph executor");
    bool leaf=false,dynamic_child=false,graph_agent=false;std::optional<DynamicPlanCapabilities> child_caps;std::optional<DynamicPresetCapability> child_preset;
    if(!admitted.parent_id.empty()){
        const auto admission=persistence_.child_admission(id).get();dynamic_child=admission.kind==ChildAdmissionKind::dynamic_agent;
        graph_agent=admission.kind==ChildAdmissionKind::graph_agent;
        leaf=admission.kind==ChildAdmissionKind::delegated_leaf||dynamic_child;
        if(admission.kind!=ChildAdmissionKind::graph_agent&&!leaf)throw std::invalid_argument("Only admitted agent children can invoke the model engine");
        if(leaf&&(!budget||budget->root_id()!=admission.root_run_id||settings_.planning||settings_.delegation||!settings_.selectable_models.empty()))throw std::invalid_argument("Agent child requires its exact bounded root owner");
        if(!dynamic_child&&leaf&&(admission.preset_id!="workspace.inspect"||admission.preset_revision!=1||settings_.approved_edits||!settings_.process_profiles.empty()||!settings_.mcp_servers.empty()||settings_.max_turns>4))throw std::invalid_argument("Delegated leaf requires its exact bounded read-only owner");
        if(dynamic_child){
            const auto plan=persistence_.dynamic_plan(admission.plan_id).get();
            const auto node=std::find_if(plan.nodes.begin(),plan.nodes.end(),[&](const auto& value){return value.child_run_id==id;});
            const auto preset=std::find_if(plan.capabilities.presets.begin(),plan.capabilities.presets.end(),[&](const auto& value){return value.id==admission.preset_id&&value.revision==admission.preset_revision;});
            if(node==plan.nodes.end()||preset==plan.capabilities.presets.end()||plan.root_run_id!=budget->root_id()||plan.state!="active"||
                node->state!=DynamicNodeState::claimed||node->claim_id!=admission.claim_id||node->definition_revision!=admission.definition_revision||
                node->claim_revision!=admission.claim_revision||node->definition.label!=admission.node_label||
                node->definition.preset_id!=preset->id||node->definition.preset_revision!=preset->revision||
                admission.backend_identity!=preset->backend_identity||settings_.max_turns!=static_cast<std::size_t>(preset->turn_limit))
                throw std::invalid_argument("Dynamic child admission does not match its native claim");
            child_caps=plan.capabilities;child_preset=*preset;
            try{
                if(!workspace_||workspace_->identity()!=child_caps->workspace_identity||
                    agent_authority_identity(settings_,settings_.provider.model,workspace_->identity(),agent_authority_credentials(persistence_,settings_))!=preset->backend_identity)
                    throw DynamicPlanUnavailable("Dynamic child authority changed before execution");
            }catch(const DynamicPlanUnavailable&){return persistence_.transition(id,RunState::queued,RunState::failed,R"({"reason":"dynamic_child_configuration_changed"})").get();}
             catch(const std::invalid_argument&){return persistence_.transition(id,RunState::queued,RunState::failed,R"({"reason":"dynamic_child_configuration_changed"})").get();}
        }
        if(!leaf&&budget&&(!graph_agent||!settings_.context||budget->root_id()!=admission.root_run_id||
            budget->snapshot().spec.policy_id!="native.graph-context"))
            throw std::invalid_argument("Registered graph agent requires its exact shared graph context budget");
    }else if(budget)throw std::invalid_argument("Normal agent root must acquire its own durable budget");
    const bool dynamic_root=admitted.parent_id.empty()&&settings_.planning&&planning_;
    const bool resumed=dynamic_root&&admitted.state==RunState::paused;
    auto provider=settings_.provider;
    const auto selected=resumed&&model_id.empty()?admitted_dynamic_model(id):model_id;
    if(!selected.empty()) {const auto allowed=models();if(std::find(allowed.begin(),allowed.end(),selected)==allowed.end()) throw std::invalid_argument("Model is not configured on this backend");provider.model=selected;}
    if(graph_agent&&settings_.context){
        if(!budget)throw std::invalid_argument("Context graph agent requires its owning shared budget");
        const auto captured=persistence_.graph_context_owner(budget->root_id()).get();
        const auto node=std::find_if(captured.authority.agents.begin(),captured.authority.agents.end(),
            [&](const auto& value){return value.node_id==admitted.node_id;});
        bool compatible=node!=captured.authority.agents.end()&&node->model_id==provider.model&&
            node->provider_identity_json==execution_identity(settings_,provider.model)&&
            node->turn_limit==static_cast<std::int64_t>(settings_.max_turns);
        if(compatible)try{compatible=node->backend_identity==context_authority_identity(settings_,provider.model,
            workspace_?workspace_->identity():"workspace:none",agent_authority_credentials(persistence_,settings_));}
        catch(const NotFound&){compatible=false;}
        catch(const Conflict&){compatible=false;}
        if(compatible)try{for(const auto& profile:settings_.process_profiles)
            if(profile.executable_id.empty()||ForegroundProcess::executable_identity(profile.executable)!=profile.executable_id){compatible=false;break;}}
        catch(const ProcessBeforeDispatchError&){compatible=false;}
        catch(const std::filesystem::filesystem_error&){compatible=false;}
        if(!compatible)return persistence_.transition(id,RunState::queued,RunState::failed,R"({"reason":"graph_child_configuration_changed"})").get();
    }
    // Claim outside the failure handler. A duplicate worker losing this update
    // must not fail the run that another worker already owns.
    if(token.stop_requested()){
        // The service's owner mutex distinguishes an explicit controller cancel
        // from shutdown/fault stop. A stopped wake has claimed no new segment.
        if(resumed)return admitted;
        return persistence_.transition(id,RunState::queued,RunState::cancelled,R"({"reason":"cancelled_before_start"})").get();
    }
    std::optional<DynamicResumeRecord> resume;std::optional<DynamicPlanCapabilities> root_caps;
    const auto initial_active_started=std::chrono::steady_clock::now();
    if(resumed){
        validate_dynamic_owner(id,provider.model);
        const auto plan=persistence_.dynamic_plan_for_root(id).get();if(!plan)throw DynamicPlanUnavailable("Paused dynamic owner has no plan");
        const auto saved=persistence_.root_budget(id).get();
        DynamicResumeSpec spec{plan->id,operation_id(),plan->capabilities.backend_identity,plan->revision,plan->state_sequence,saved.revision};
        spec.context_pin=persistence_.dynamic_context_pause(id).get();
        if(settings_.context&&!spec.context_pin)throw DynamicPlanUnavailable("Context-enabled pause has no exact durable context pin");
        const auto active_started=std::chrono::steady_clock::now();
        resume=persistence_.resume_dynamic_owner(std::move(spec)).get();root_caps=resume->plan.capabilities;
        try{budget=std::make_shared<RootExecutionBudget>(persistence_,persistence_.root_budget(id).get(),
            active_started+std::chrono::milliseconds(resume->segment.remaining_active_ms),token,resume->segment,root_caps->backend_identity);}
        catch(...){throw DynamicOutcomeUnrecorded("Resumed dynamic segment lacks its live clock owner; recovery is required");}
        if(resume->context_pin)budget->adopt_context_pin(*resume->context_pin);
    }else persistence_.transition(id,RunState::queued,RunState::running).get();
    std::stop_source linked;
    std::stop_callback external(token,[&]{linked.request_stop();});
    std::atomic<bool> timed_out=false;
    std::jthread timer;
    token=linked.get_token();
    auto terminate=[&](RunState state,const Json& reason) {
        if(dynamic_root)return persistence_.retire_dynamic_owner(id,state,reason.dump(),
            budget&&budget->segment_open()?budget->segment_id():std::string{},
            budget&&budget->segment_open()?budget->active_elapsed_ms():0).get();
        return persistence_.transition(id,RunState::running,state,reason.dump()).get();
    };
    try {
        const auto local_deadline=std::chrono::steady_clock::now()+settings_.run_timeout;
        if(!leaf&&admitted.parent_id.empty()&&execution_budget(provider.model)){
            const auto saved=persistence_.root_budget(id).get();const auto expected=execution_budget(provider.model);
            if(saved.spec.workspace_identity!=expected->workspace_identity||saved.spec.policy_id!=expected->policy_id||saved.spec.policy_revision!=expected->policy_revision||saved.spec.provider_identity_json!=expected->provider_identity_json||saved.spec.max_children!=expected->max_children||saved.spec.max_parallel!=expected->max_parallel||saved.spec.max_model_calls!=expected->max_model_calls||saved.spec.wall_limit_ms!=expected->wall_limit_ms)throw Conflict("Root execution no longer matches its admitted immutable policy");
            if(dynamic_root){
                validate_dynamic_owner(id,provider.model);root_caps=persistence_.dynamic_capabilities(id).get();
                if(!resumed){
                    DynamicSegmentSpec spec{id,operation_id(),root_caps->backend_identity,saved.revision};
                    const auto segment=persistence_.open_dynamic_budget_segment(std::move(spec)).get();
                    try{budget=std::make_shared<RootExecutionBudget>(persistence_,persistence_.root_budget(id).get(),
                        initial_active_started+std::chrono::milliseconds(segment.remaining_active_ms),token,segment,root_caps->backend_identity);}
                    catch(...){throw DynamicOutcomeUnrecorded("Opened dynamic segment lacks its live clock owner; recovery is required");}
                }
            }else budget=std::make_shared<RootExecutionBudget>(persistence_,saved,local_deadline,token);
        }
        if(leaf){const auto saved=budget->snapshot();if(!workspace_||workspace_->identity()!=saved.spec.workspace_identity)throw std::invalid_argument("Delegated leaf workspace identity changed");
            if(execution_identity(settings_,provider.model)!=saved.spec.provider_identity_json)throw std::invalid_argument("Delegated leaf provider identity changed");
            persistence_.append_event(id,dynamic_child?"agent.plan.preset":"agent.delegation.preset",Json{{"preset_id",dynamic_child?child_preset->id:"workspace.inspect"},{"preset_revision",1},{"root_run_id",budget->root_id()},{"tool_policy",dynamic_child&&!child_preset->readonly?"approval_controlled":"read_only"}}.dump()).get();}
        const auto run_deadline=budget?budget->deadline():local_deadline;
        timer=std::jthread([&,run_deadline](std::stop_token ending) {
            std::mutex mutex;std::condition_variable_any changed;std::unique_lock lock(mutex);
            changed.wait_until(lock,ending,run_deadline,[]{return false;});
            if(!ending.stop_requested()) {timed_out=true;linked.request_stop();}
        });
        ModelRequest request;request.include_usage=settings_.provider.stream_usage==Capability::supported;
        request.max_output_tokens=settings_.max_output_tokens;
        auto agent_instructions=settings_.instructions;
        if(!graph_instructions.empty()){
            agent_instructions.append("\n\nSelected agent instructions (trusted backend snapshot):\n");
            agent_instructions+=graph_instructions;
        }
        auto instructions=agent_instructions;std::unique_ptr<RepositoryInstructionContext> repository_context;
        if(workspace_){
            const auto sources=workspace_->repository_instructions(".",token);auto metadata=Json::array();
            for(const auto& source:sources){
                metadata.push_back({{"path",source.path},{"workspace_id",source.workspace_id},{"file_id",source.file_id},{"content_sha256",source.content_sha256},{"byte_count",source.content.size()}});
            }
            persistence_.append_event(id,"agent.repository_instructions",Json{{"scope","workspace_root"},{"snapshot","run_start"},{"sources",std::move(metadata)}}.dump()).get();
            repository_context=std::make_unique<RepositoryInstructionContext>(*workspace_,sources);
            repository_context->skills().restore(persistence_.initialize_run_skills(id,workspace_->identity()).get(),token);
            instructions+=repository_context->prepare(token);
        }
        if(!settings_.instruction_policy.instructions.empty()){instructions.append(instruction_prefix);instructions+=settings_.instruction_policy.instructions;}
        if(instructions.size()>65536)throw std::invalid_argument("Combined agent instructions exceed limits");
        if(!instructions.empty()) request.messages.push_back({MessageRole::system,std::move(instructions)});
        if(settings_.instruction_policy.revision>0)persistence_.append_event(id,"agent.instructions",Json{{"revision",settings_.instruction_policy.revision},{"byte_count",settings_.instruction_policy.instructions.size()},{"scope","server"},{"runtime_state","startup_snapshot"}}.dump()).get();
        const auto instruction_messages=request.messages.size();
        auto reload_history=[&]{
            request.messages.resize(instruction_messages);request.canonical_window.reset();
            if(!settings_.context)for(const auto& stored:persistence_.run_history(id).get())request.messages.push_back(message(stored));
        };
        reload_history();
        if(workspace_){request.tools=workspace_->definitions();for(auto& definition:SkillContext::definitions())request.tools.push_back(std::move(definition));}
        if(settings_.approved_edits) request.tools.push_back(EditExecutor::definition());
        if(settings_.approved_edits) request.tools.push_back(CreateExecutor::definition());
        if(settings_.approved_edits) request.tools.push_back(PatchTool::definition());
        if(process_)request.tools.push_back(process_->definition());
        struct McpRuntime {std::unique_ptr<McpToolClient> client;std::unique_ptr<McpToolRegistry> registry;};
        std::vector<McpRuntime> mcp_runtimes;std::map<std::string,McpToolRegistry*> mcp_tools;Json mcp_bindings=Json::array();
        const auto native_tools=request.tools;
        auto close_mcp=[&]{mcp_tools.clear();mcp_runtimes.clear();mcp_bindings=Json::array();};
        auto discover_mcp=[&]{
        close_mcp();request.tools=native_tools;
        for(const auto& server:settings_.mcp_servers) {
            if(!server.enabled)continue;cancelled(token);
            persistence_.append_event(id,"mcp.connecting",Json{{"server_id",server.id},{"config_revision",server.revision}}.dump()).get();
            cancelled(token);if(std::chrono::steady_clock::now()>=run_deadline)throw RootBudgetDeadlineExceeded("Root execution deadline elapsed before MCP launch");
            McpRuntime runtime;runtime.client=connect_mcp_client(server,persistence_,run_deadline,token);
            persistence_.append_event(id,"mcp.connected",Json{{"server_id",server.id},{"config_revision",server.revision},{"protocol_version",runtime.client->server().protocol_version}}.dump()).get();
            runtime.registry=std::make_unique<McpToolRegistry>(*runtime.client,persistence_,*workspace_,server.id,server.revision,run_deadline,token);
            const auto definitions=runtime.registry->definitions();
            for(const auto& definition:definitions){if(request.tools.size()>=64 || !mcp_tools.emplace(definition.name,runtime.registry.get()).second)throw std::invalid_argument("Model tool catalogue exceeds limits or has an alias collision");request.tools.push_back(definition);}
            const auto bindings=Json::parse(runtime.registry->approval_bindings_json());
            if(!bindings.is_array())throw DynamicPlanUnavailable("Native MCP registry returned invalid approval bindings");
            for(const auto& binding:bindings)mcp_bindings.push_back(binding);
            persistence_.append_event(id,"mcp.discovered",Json{{"server_id",server.id},{"config_revision",server.revision},{"tool_count",definitions.size()}}.dump()).get();
            mcp_runtimes.push_back(std::move(runtime));
        }
        };
        auto seal_catalogue=[&]{
            if(dynamic_child)verify_child_catalogue(request.tools,mcp_bindings,*child_caps,*child_preset);
            if(dynamic_root){
                validate_dynamic_owner(id,provider.model);
                auto presets=root_caps->presets;
                for(auto& preset:presets){
                    if(!preset.readonly){preset.tools.clear();for(const auto& definition:request.tools)preset.tools.push_back(definition.name);}
                    std::sort(preset.tools.begin(),preset.tools.end());
                    const auto child=dynamic_child_settings(settings_,preset,provider.model);
                    preset.backend_identity=agent_authority_identity(child,provider.model,root_caps->workspace_identity,agent_authority_credentials(persistence_,child));
                }
                root_caps=persistence_.finalize_dynamic_capabilities(id,root_caps->backend_identity,tool_catalogue(request.tools,mcp_bindings),std::move(presets)).get();
            }
        };
        auto offer_orchestration=[&]{
        if(budget&&!leaf&&delegation_&&settings_.delegation){if(request.tools.size()>=64)throw std::invalid_argument("Model tool catalogue exceeds limits");request.tools.push_back(DelegationExecutor::definition());}
        if(dynamic_root){for(auto& definition:dynamic_plan_tool_definitions(*root_caps)){if(request.tools.size()>=64)throw std::invalid_argument("Model tool catalogue exceeds limits");request.tools.push_back(std::move(definition));}}
        };
        auto refresh_tools=[&]{discover_mcp();seal_catalogue();offer_orchestration();};
        refresh_tools();
        std::string planning_continuation;
        auto commit_plan_result=[&](const DynamicExecutionResult& actual){
            if(actual.paused||actual.root.id!=id||actual.call_id.empty()||actual.result_json.empty())throw DynamicOutcomeUnrecorded("Dynamic report does not match its actual running owner");
            persistence_.commit_dynamic_tool_turn(id,actual.call_id).get();
            reload_history();planning_continuation=actual.call_id;
        };
        if(resume){
            // Idle MCP peers retire before a clean pause closes active time.
            close_mcp();
            const auto actual=planning_->resume(id,resume->pending_call.id,settings_,*root_caps,budget,token);
            if(actual.paused)return actual.root;
            commit_plan_result(actual);refresh_tools();
        }
        std::string delivered_repository_metadata;
        std::unique_ptr<ContextManager> context_manager;
        if(settings_.context)context_manager=std::make_unique<ContextManager>(persistence_,provider,*settings_.context,message);
        for(std::size_t turn=0;turn<settings_.max_turns;++turn) {
            cancelled(token);
            if(!leaf&&budget&&delegation_&&!delegation_->healthy())throw DelegationOutcomeUnrecorded("Native delegation owner is faulted");
            if(dynamic_root){if(!planning_->healthy())throw DynamicOutcomeUnrecorded("Native planning owner is faulted");validate_dynamic_owner(id,provider.model);}
            if(repository_context){
                auto current=agent_instructions+repository_context->prepare(token);
                if(!settings_.instruction_policy.instructions.empty()){current.append(instruction_prefix);current+=settings_.instruction_policy.instructions;}
                if(current.size()>65536)throw ToolFileError("Combined agent instructions exceed limits");
                request.messages.front().content=std::move(current);
                const auto metadata=repository_context->metadata();if(metadata!=delivered_repository_metadata){persistence_.append_event(id,"agent.repository_scope",metadata).get();delivered_repository_metadata=metadata;}
            }
            std::optional<SecretBytes> credential;
            if(settings_.credential) credential.emplace(persistence_.resolve_credential(settings_.credential->scope,settings_.credential->id,settings_.credential->purpose).get());
            auto response_started=std::chrono::steady_clock::now();
            std::optional<std::int64_t> first_token_ms;
            std::optional<ModelCallReservation> reservation;
            std::optional<PreparedModelContext> prepared_context;
            std::optional<ContextOwner> context_owner;
            auto trusted_current=request;trusted_current.messages.resize(instruction_messages);trusted_current.canonical_window.reset();
            if(context_manager){
                if(!budget)throw ContextUnavailable("Context execution has no owned native budget");
                ContextOwner owner;owner.scope={admitted.parent_id.empty()?ContextScopeKind::session:ContextScopeKind::execution,
                    admitted.parent_id.empty()?admitted.session_id:id};
                owner.root_run_id=budget->root_id();owner.owner_run_id=id;owner.role=leaf||graph_agent?ModelCallRole::leaf:ModelCallRole::parent;
                owner.binding.provider_identity_json=execution_identity(settings_,provider.model);
                owner.binding.authority_identity=context_authority_identity(settings_,provider.model,workspace_?workspace_->identity():"workspace:none",
                    agent_authority_credentials(persistence_,settings_));
                const auto& p=settings_.context->compaction;owner.binding.policy_id=p.id;owner.binding.policy_revision=p.revision;
                owner.binding.strategy_id=p.strategy_id;owner.binding.strategy_revision=p.strategy_revision;
                if(!planning_continuation.empty())owner.planning_call_id=planning_continuation;
                context_owner=owner;prepared_context=context_manager->prepare(owner,trusted_current,*budget,credential?&*credential:nullptr,token);
                request=prepared_context->request;reservation=prepared_context->inference;
                if(dynamic_root)budget->pin_context(prepared_context->snapshot);
                planning_continuation.clear();
            }else if(budget){
                reservation=planning_continuation.empty()?budget->reserve(id,leaf?ModelCallRole::leaf:ModelCallRole::parent,token):budget->reserve_continuation(planning_continuation,token);
                planning_continuation.clear();
            }
            ModelCompletion response;Json reply;
            bool durable_output=false,attempt_finished=false,rebuild_used=false;
            auto finish_context_attempt=[&](InferenceAttemptOutcome outcome,const std::optional<std::string>& actual={},const std::string& error_code={}){
                try{persistence_.finish_inference_attempt({prepared_context->inference_step_id,reservation->attempt_id,outcome,
                    actual,error_code,std::chrono::ceil<std::chrono::milliseconds>(std::chrono::steady_clock::now()-response_started).count()}).get();attempt_finished=true;}
                catch(...){throw ContextOutcomeUnrecorded("Inference outcome could not be recorded");}
            };
            try{for(;;){
            response_started=std::chrono::steady_clock::now();first_token_ms.reset();durable_output=false;attempt_finished=false;
            if(reservation)budget->start(*reservation,token);
            try{
            response=complete_model(provider,request,credential?&*credential:nullptr,[&](const ModelEvent& event) {
                if(!first_token_ms && (event.kind=="model.text" || event.kind=="model.refusal" || event.kind=="model.tool_delta" || event.kind=="model.reasoning")) first_token_ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-response_started).count();
                cancelled(token);persistence_.append_event(id,event.kind,event.json).get();
                if(event.kind=="model.text"||event.kind=="model.refusal"||event.kind=="model.tool_delta"||event.kind=="model.reasoning")durable_output=true;
            },token);
            }catch(const ProviderHttpError& error){
                if(context_manager&&!rebuild_used&&settings_.context->compaction.automatic&&!durable_output&&
                   error.status==400&&error.code=="context_length_exceeded"){
                    finish_context_attempt(InferenceAttemptOutcome::context_overflow,{},"context_length_exceeded");
                    prepared_context=context_manager->rebuild(*context_owner,trusted_current,*prepared_context,*budget,credential?&*credential:nullptr,token);
                    request=prepared_context->request;reservation=prepared_context->inference;rebuild_used=true;
                    if(dynamic_root)budget->pin_context(prepared_context->snapshot);
                    continue;
                }
                throw;
            }
            const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-response_started).count();
            reply=assistant(response,provider.model,elapsed,first_token_ms);
            const auto context=provider_context_json(settings_,provider.model);if(!context.empty())reply["provider_context"]=Json::parse(context);
            if(prepared_context)finish_context_attempt(InferenceAttemptOutcome::completed,reply.dump());
            else if(reservation)budget->finish(*reservation,reply.dump());
            break;
            }}catch(const BudgetOutcomeUnrecorded&){throw;}
             catch(const ContextOutcomeUnrecorded&){throw;}
             catch(...){const auto failure=std::current_exception();
                if(reservation&&!attempt_finished){if(prepared_context)finish_context_attempt(token.stop_requested()?InferenceAttemptOutcome::cancelled:InferenceAttemptOutcome::failed);
                    else budget->finish(*reservation);}
                std::rethrow_exception(failure);}
            cancelled(token);
            if(response.finish_reason=="stop"){
                if(dynamic_root){
                    const auto plan=persistence_.dynamic_plan_for_root(id).get();
                    if(plan&&!inspect_dynamic_plan(*plan).finished)return terminate(RunState::failed,{{"reason","dynamic_plan_incomplete"}});
                    return persistence_.complete_dynamic_owner(id,reply.dump(),budget->segment_id(),budget->active_elapsed_ms()).get();
                }
                return persistence_.complete_run(id,reply.dump()).get();
            }
            if(response.finish_reason!="tool_calls" || !workspace_) throw ModelProtocolError("Model did not produce a complete supported turn");
            const auto is_planning=[](const auto& call){return call.name=="plan_tasks"||call.name=="revise_plan"||call.name=="inspect_plan";};
            if(std::any_of(response.tool_calls.begin(),response.tool_calls.end(),is_planning)){
                if(!dynamic_root||response.tool_calls.size()!=1)throw ModelProtocolError("Planning must be the sole tool call of its owning ordinary Agent turn");
                const auto& call=response.tool_calls.front();const auto activity=id+":plan:"+call.id;
                persistence_.append_event(id,"tool.started",Json{{"activity_id",activity},{"call_id",call.id},{"name",call.name},{"arguments",Json::parse(call.arguments_json)},{"arguments_json",call.arguments_json}}.dump()).get();
                close_mcp();DynamicExecutionResult actual;
                try{actual=planning_->invoke(id,call,reply.dump(),reservation->attempt_id,settings_,*root_caps,budget,token);}
                catch(const DynamicPlanRejected& error){
                    persistence_.record_rejected_dynamic_tool_turn(id,reservation->attempt_id,reply.dump(),error.code).get();
                    reload_history();refresh_tools();continue;
                }
                if(actual.paused)return actual.root;
                if(!actual.call_id.empty())commit_plan_result(actual);
                else{
                    const auto output=Json::parse(actual.result_json);
                    persistence_.append_event(id,"tool.completed",Json{{"activity_id",activity},{"call_id",call.id},{"name",call.name},{"data",output}}.dump()).get();
                    persistence_.record_tool_turn(id,reply.dump(),{Json{{"content",output.dump()},{"tool_call_id",call.id}}.dump()}).get();reload_history();
                }
                refresh_tools();continue;
            }
            std::vector<std::string> results;std::vector<ModelMessage> continuation;
            continuation.push_back({MessageRole::assistant,response.content,response.tool_calls,{},response.refusal,response.provider_items_json});
            std::size_t index=0;
            for(const auto& call:response.tool_calls) {
                cancelled(token);
                const auto activity=id+":"+(reservation?reservation->attempt_id:std::to_string(turn))+":"+std::to_string(index++);
                persistence_.append_event(id,"tool.started",Json{{"activity_id",activity},{"call_id",call.id},{"name",call.name},{"arguments",Json::parse(call.arguments_json)}}.dump()).get();
                Json output;bool success=false;
                try {
                    bool guidance_ready=true;InstructionPrecondition guidance;std::optional<PreparedPatch> prepared_patch;
                    if(repository_context&&call.name!="list_skills"&&call.name!="load_skill")guidance_ready=repository_context->skills().ready(token);
                    if(repository_context && (call.name=="read_file" || call.name=="edit_file" || call.name=="create_file" || call.name=="list_files" || call.name=="run_process")){
                        Json args;try{args=Json::parse(mcp_compact_object(call.arguments_json));}catch(const McpProtocolError&){throw std::invalid_argument("Invalid scoped tool JSON");}
                        const auto field=call.name=="run_process"?"workdir":"path";
                        std::string scope=".";
                        if(args.contains(field)){if(!args[field].is_string())throw std::invalid_argument("Invalid scoped tool path");scope=args[field].get<std::string>();}
                        else if(call.name!="run_process")throw std::invalid_argument("Scoped tool requires a path");
                        if(call.name=="create_file"&&args.contains("create_parents")&&!args["create_parents"].is_boolean())throw std::invalid_argument("Invalid creation parent option");
                        if(call.name=="create_file"&&args.value("create_parents",false))scope=workspace_->creation_directory(scope,token);
                        else if(call.name!="run_process" && call.name!="list_files")scope=RepositoryInstructionContext::file_directory(scope);
                        guidance_ready=guidance_ready&&repository_context->ready(scope,token);
                        if(guidance_ready && (call.name=="edit_file" || call.name=="create_file" || call.name=="run_process"))guidance=repository_context->precondition(scope);
                    }
                    if(repository_context&&call.name=="load_skill")guidance_ready=repository_context->ready(repository_context->skills().activation_directory(call.arguments_json,token),token);
                    if(repository_context&&mcp_tools.contains(call.name)){guidance_ready=guidance_ready&&repository_context->ready(".",token);if(guidance_ready)guidance=repository_context->precondition(".");}
                    if(call.name=="apply_patch"&&settings_.approved_edits&&repository_context){prepared_patch=PatchTool(persistence_,*workspace_,*repository_context).prepare(call.arguments_json,token);guidance_ready=prepared_patch.has_value()&&guidance_ready;}
                    if(!guidance_ready){output={{"error",{{"code","repository_instructions_required"},{"message","No requested action or approval proposal occurred. Updated repository or skill guidance will be supplied in the next model request; reconsider this call using it."}}}};}
                    else if(call.name=="list_skills"&&repository_context){try{if(mcp_compact_object(call.arguments_json)!="{}")throw std::invalid_argument("Skill listing takes no arguments");}catch(const McpProtocolError&){throw std::invalid_argument("Invalid skill listing arguments");}output=Json::parse(repository_context->skills().catalogue_json(token));}
                    else if(call.name=="load_skill"&&repository_context){const auto base_bytes=agent_instructions.size()+(settings_.instruction_policy.instructions.empty()?0:instruction_prefix.size()+settings_.instruction_policy.instructions.size());output=Json::parse(repository_context->activate_skill(call.arguments_json,base_bytes,token));}
                    else if(call.name=="edit_file" && settings_.approved_edits) {
                        const auto expiry=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+settings_.run_timeout.count();
                        output=Json::parse(EditExecutor(persistence_,*workspace_).invoke(operation_id(),id,call.arguments_json,expiry,token,std::move(guidance)));
                    } else if(call.name=="create_file" && settings_.approved_edits) {
                        const auto expiry=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+settings_.run_timeout.count();
                        output=Json::parse(CreateExecutor(persistence_,*workspace_).invoke(operation_id(),id,call.arguments_json,expiry,token,std::move(guidance)));
                    } else if(call.name=="apply_patch"&&settings_.approved_edits&&repository_context&&prepared_patch){
                        const auto expiry=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+settings_.run_timeout.count();
                        output=Json::parse(PatchTool(persistence_,*workspace_,*repository_context).execute(operation_id(),id,std::move(*prepared_patch),expiry,token));
                    } else if(call.name=="run_process" && process_) {
                        const auto expiry=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+settings_.run_timeout.count();
                        output=Json::parse(process_->invoke(operation_id(),id,call.arguments_json,expiry,token,std::move(guidance)));
                    } else if(call.name=="delegate_tasks"&&budget&&!leaf&&delegation_&&settings_.delegation){
                        auto frozen=settings_;frozen.provider=provider;
                        output=Json::parse(delegation_->invoke(id,call,reply.dump(),frozen,budget,token));
                    } else if(const auto registered=mcp_tools.find(call.name);registered!=mcp_tools.end()) {
                        const auto expiry=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+settings_.run_timeout.count();
                        output=Json::parse(registered->second->invoke(operation_id(),id,call.name,call.arguments_json,expiry,run_deadline,token,std::move(guidance)));
                    } else output=Json::parse(workspace_->invoke(call.name,call.arguments_json,token));
                    success=guidance_ready&&(call.name!="apply_patch"||output.value("state",std::string{})=="succeeded");
                }
                catch(const PermissionCancelled&) {throw;}
                catch(const RootBudgetExhausted&){output={{"error",{{"code","delegation_budget_exhausted"},{"message","No additional children were admitted; the root execution allowance is exhausted"}}}};}
                catch(const DelegationCapacityUnavailable&){output={{"error",{{"code","delegation_capacity_unavailable"},{"message","No children were admitted; the native leaf queue is full"}}}};}
                catch(const PermissionDenied&) {output={{"error",{{"code","permission_denied"},{"message","Controller denied this operation; the requested effect was not dispatched"}}}};}
                catch(const PermissionExpired&) {output={{"error",{{"code","permission_expired"},{"message","Approval expired; the requested effect was not dispatched"}}}};}
                catch(const McpEffectNotDispatched&) {output={{"error",{{"code","mcp_not_dispatched"},{"message","MCP request was not dispatched"}}}};}
                catch(const ProcessBeforeDispatchError&) {output={{"error",{{"code","process_not_dispatched"},{"message","Process did not execute; launch preconditions were unavailable or changed"}}}};}
                catch(const ToolGuidanceChanged&) {output={{"error",{{"code","repository_instructions_required"},{"message","Approved proposal was retired without dispatch because repository guidance changed. Updated guidance will be supplied before a new call and new approval."}}}};}
                catch(const ToolContentConflict&) {output={{"error",{{"code","content_conflict"},{"message","File contents, identity, replacement count or target absence no longer match the operation"}}}};}
                catch(const ToolCancelled&) {throw;}
                catch(const ToolAccessDenied&) {output={{"error",{{"code","access_denied"},{"message","Workspace policy denied this operation"}}}};}
                catch(const ToolFileError&) {output={{"error",{{"code","file_unavailable"},{"message","File is unavailable, binary, outside limits or unreadable"}}}};}
                catch(const std::invalid_argument&) {output={{"error",{{"code","invalid_arguments"},{"message","Arguments do not match the tool contract"}}}};}
                cancelled(token);
                persistence_.append_event(id,success?"tool.completed":"tool.failed",Json{{"activity_id",activity},{"call_id",call.id},{"name",call.name},{"data",output}}.dump()).get();
                const auto content=output.dump();results.push_back(Json{{"content",content},{"tool_call_id",call.id}}.dump());
                continuation.push_back({MessageRole::tool,content,{},call.id});
            }
            // The assistant call and all matching results commit together; a
            // cancelled/crashed read batch cannot leave dangling call messages.
            const bool delegated=std::any_of(response.tool_calls.begin(),response.tool_calls.end(),[](const auto& call){return call.name=="delegate_tasks";});
            try{persistence_.record_tool_turn(id,reply.dump(),std::move(results),repository_context?std::optional<SkillSelections>{repository_context->skills().selections()}:std::nullopt).get();}
            catch(...){if(delegated)throw DelegationOutcomeUnrecorded("Delegation parent conversation could not be committed; recovery is required");throw;}
            if(!context_manager)for(auto& next:continuation) request.messages.push_back(std::move(next));
        }
        return terminate(RunState::failed,{{"reason","model_turn_limit"}});
    } catch(const TransportCancelled&) {return terminate(timed_out?RunState::failed:RunState::cancelled,{{"reason",timed_out?"agent_timeout":"cancelled"}});}
      catch(const RootBudgetDeadlineExceeded&){return terminate(RunState::failed,{{"reason","agent_timeout"}});}
      catch(const RootBudgetExhausted&){return terminate(RunState::failed,{{"reason","execution_budget_exhausted"}});}
      catch(const BudgetOutcomeUnrecorded&){throw;}
      catch(const ContextOutcomeUnrecorded&){throw;}
      catch(const ContextCapacityExceeded&){return terminate(RunState::failed,{{"reason","context_capacity_exceeded"}});}
      catch(const ModelRequestCapacityExceeded&){return terminate(RunState::failed,{{"reason","context_capacity_exceeded"}});}
      catch(const ContextUnavailable&){return terminate(RunState::failed,{{"reason","context_unavailable"}});}
      catch(const DelegationOutcomeUnrecorded&){throw;}
      catch(const DynamicOutcomeUnrecorded&){throw;}
      catch(const NativeChildOutcomeUnrecorded&){throw;}
      catch(const DynamicHumanExpired&){return terminate(RunState::failed,{{"reason","human_input_expired"}});}
      catch(const DynamicPlanUnavailable&){return terminate(RunState::failed,{{"reason","dynamic_configuration_changed"}});}
      catch(const PermissionCancelled&) {return terminate(timed_out?RunState::failed:RunState::cancelled,{{"reason",timed_out?"agent_timeout":"cancelled"}});}
      catch(const ToolGuidanceChanged&) {return terminate(RunState::failed,{{"reason","skill_context_unavailable"}});}
      catch(const ToolMutationUncertain&) {return terminate(RunState::failed,{{"reason","file_effect_uncertain"}});}
      catch(const EditOutcomeUnrecorded&) {throw;} // Leave claim for recovery; AgentService degrades admission.
      catch(const McpOutcomeUnrecorded&) {throw;}
      catch(const ProcessOutcomeUnrecorded&) {throw;}
      catch(const ProcessEffectUncertain&) {return terminate(RunState::failed,{{"reason","process_effect_uncertain"}});}
      catch(const McpEffectUncertain&) {return terminate(RunState::failed,{{"reason","mcp_effect_uncertain"}});}
      catch(const SchemaEvaluationCancelled&) {return terminate(timed_out?RunState::failed:RunState::cancelled,{{"reason",timed_out?"agent_timeout":"cancelled"}});}
      catch(const SchemaEvaluationFailure&) {return terminate(RunState::failed,{{"reason","mcp_schema_evaluation_unavailable"}});}
      catch(const McpTransportCancelled&) {return terminate(timed_out?RunState::failed:RunState::cancelled,{{"reason",timed_out?"agent_timeout":"cancelled"}});}
      catch(const McpTransportError&) {return terminate(RunState::failed,{{"reason","mcp_transport_failure"}});}
      catch(const McpProtocolError&) {return terminate(RunState::failed,{{"reason","mcp_protocol_failure"}});}
      catch(const ToolCancelled&) {return terminate(timed_out?RunState::failed:RunState::cancelled,{{"reason",timed_out?"agent_timeout":"cancelled"}});}
      catch(const ProviderHttpError& error) {
          Json detail={{"reason","provider_http_error"},{"status",error.status}};
          if(!error.type.empty()) detail["provider_error_type"]=error.type;
          if(!error.code.empty()) detail["provider_error_code"]=error.code;
          if(!error.param.empty()) detail["provider_error_param"]=error.param;
          return terminate(RunState::failed,detail);
      }
      catch(const TransportTimeout&) {return terminate(RunState::failed,{{"reason","provider_timeout"}});}
      catch(const TransportError&) {return terminate(RunState::failed,{{"reason","provider_transport_error"}});}
      catch(const ModelProtocolError& error) {Json detail={{"reason","model_protocol_error"}};const auto code=model_protocol_diagnostic(error);if(!code.empty())detail["protocol_error_code"]=std::string(code);return terminate(RunState::failed,detail);}
      catch(const IncompatibleProviderHistory&) {return terminate(RunState::failed,{{"reason","incompatible_provider_history"}});}
      catch(const std::exception&) {return terminate(RunState::failed,{{"reason","agent_error"}});}
}
}
