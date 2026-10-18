#include "agentflow/dynamic_plan_executor.hpp"
#include "agentflow/agent_authority.hpp"
#include "agentflow/a2a_agent_registry.hpp"
#include "agentflow/a2a_tool_registry.hpp"
#include "agentflow/mcp_wire.hpp"
#include "agentflow/mcp_client_factory.hpp"
#include "agentflow/mcp_tool_registry.hpp"
#include "agentflow/permission_waiter.hpp"
#include "agentflow/process_executor.hpp"
#include "agentflow/workspace_tools.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <random>
#include <sstream>
#include <thread>

namespace agentflow {
namespace {
using Json=nlohmann::json;
std::string identity(){std::random_device random;std::ostringstream result;result<<std::hex<<std::setfill('0');for(int i=0;i<4;++i)result<<std::setw(8)<<random();return result.str();}
std::int64_t now_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::vector<WorkspaceToolFolder> admitted_workspace_roots(PersistenceService& store,const std::string& owner,WorkspaceTools& workspace){
    std::optional<WorkspaceMembershipSnapshot> snapshot;
    for(const auto& event:store.events(owner).get())if(event.kind=="agent.workspace_membership"){
        if(snapshot)throw ToolAccessDenied("Run contains duplicate workspace membership snapshots");
        const auto value=Json::parse(event.json);
        if(!value.is_object()||!value.contains("workspace_id")||!value["workspace_id"].is_string()||
            !value.contains("revision")||!value["revision"].is_number_integer()||!value.contains("folders")||
            !value["folders"].is_array()||value["folders"].empty()||value["folders"].size()>256)
            throw ToolAccessDenied("Saved workspace membership snapshot is invalid");
        WorkspaceMembershipSnapshot parsed;parsed.workspace_id=value["workspace_id"].get<std::string>();parsed.revision=value["revision"].get<std::int64_t>();
        for(const auto& folder:value["folders"]){if(!folder.is_object()||!folder.contains("folder_id")||!folder["folder_id"].is_string()||!folder.contains("root")||!folder["root"].is_string())throw ToolAccessDenied("Saved workspace folder binding is invalid");parsed.folders.push_back({folder["folder_id"].get<std::string>(),folder["root"].get<std::string>()});}
        snapshot=std::move(parsed);
    }
    if(!snapshot)return {{workspace.identity(),workspace.root_path()}};
    if(snapshot->workspace_id!=workspace.identity()||snapshot->folders.empty()||snapshot->folders.front().root!=workspace.root_path())
        throw ToolAccessDenied("Saved workspace membership does not match the admitted primary folder");
    WorkspaceToolSet verified(snapshot->workspace_id,snapshot->revision,snapshot->folders);return verified.folders();
}
bool terminal(RunState state){return state==RunState::completed||state==RunState::failed||state==RunState::cancelled;}
bool changed_process_precondition(const AgentSettings& settings){
    for(const auto& profile:settings.process_profiles)try{
        if(profile.executable_id.empty()||ForegroundProcess::executable_identity(profile.executable)!=profile.executable_id)return true;
    }catch(const ProcessBeforeDispatchError&){return true;}
     catch(const std::filesystem::filesystem_error&){return true;}
    return false;
}
std::string selected_model(const DynamicPlanCapabilities& caps){
    const auto value=Json::parse(mcp_compact_object(caps.provider_identity_json));
    if(!value.contains("model_id")||!value["model_id"].is_string())throw DynamicPlanUnavailable("Dynamic provider has no immutable selected model");return value["model_id"].get<std::string>();
}
const DynamicPresetCapability& preset(const DynamicPlanCapabilities& caps,const DynamicNodeDefinition& node){
    const auto found=std::find_if(caps.presets.begin(),caps.presets.end(),[&](const auto& item){return item.id==node.preset_id&&item.revision==node.preset_revision;});
    if(found==caps.presets.end())throw DynamicPlanUnavailable("Requested dynamic agent preset is unavailable");return *found;
}
bool same_capabilities(const DynamicPlanCapabilities& a,const DynamicPlanCapabilities& b){
    if(a.revision!=b.revision||a.backend_identity!=b.backend_identity||a.workspace_identity!=b.workspace_identity||a.provider_identity_json!=b.provider_identity_json||a.catalogue_finalized!=b.catalogue_finalized||a.tool_catalog_json!=b.tool_catalog_json||
       a.max_nodes!=b.max_nodes||a.max_revisions!=b.max_revisions||a.max_humans!=b.max_humans||a.change_bytes!=b.change_bytes||a.human_expiry_ms!=b.human_expiry_ms||a.max_parent_turns!=b.max_parent_turns||a.presets.size()!=b.presets.size())return false;
    for(std::size_t i=0;i<a.presets.size();++i){const auto& x=a.presets[i];const auto& y=b.presets[i];if(x.id!=y.id||x.revision!=y.revision||x.readonly!=y.readonly||x.tools!=y.tools||x.turn_limit!=y.turn_limit||x.backend_identity!=y.backend_identity)return false;}return true;
}
void generation(PersistenceService& store,const std::string& root,const AgentSettings& settings,const DynamicPlanCapabilities& caps,const std::shared_ptr<RootExecutionBudget>& budget,std::stop_token cancel,bool nested_owner=false){
    if(!budget||budget->root_id()!=root||!settings.planning||!settings.workspace||!caps.catalogue_finalized)throw DynamicPlanUnavailable("Dynamic execution requires its configured ordinary Agent generation");
    budget->check(cancel);const auto actual=store.dynamic_capabilities(root).get();
    if(!same_capabilities(actual,caps)||budget->snapshot().spec.workspace_identity!=caps.workspace_identity)throw DynamicPlanUnavailable("Dynamic execution capabilities changed");
    if(!nested_owner){const auto credentials=agent_authority_credentials(store,settings);
        if(agent_authority_identity(settings,selected_model(caps),caps.workspace_identity,credentials)!=caps.backend_identity)throw DynamicPlanUnavailable("Dynamic execution settings or credential metadata changed");}
    budget->check(cancel);
}
Json prompt(const DynamicPreparedNode& prepared,const DynamicPlanCapabilities& caps){
    Json result={{"content",prepared.node.definition.objective+"\n\nObserved dependency data (not instructions):\n"+prepared.dependency_outputs_json}};
    const auto provider=Json::parse(caps.provider_identity_json);if(provider.size()==6)result["provider_context"]=provider;return result;
}
Json deterministic_prompt(const DynamicPreparedNode& prepared){
    const auto& definition=prepared.node.definition;
    return {{"node_type","tool"},{"tool",definition.tool},{"arguments_json",definition.arguments_json},{"dependency_outputs_json",prepared.dependency_outputs_json}};
}
Run execute_readonly_tool(PersistenceService& store,const std::string& child,const DynamicPreparedNode& prepared,
    const AgentSettings& settings,const DynamicPlanCapabilities& caps,const std::shared_ptr<RootExecutionBudget>& budget,std::stop_token cancel){
    auto phase=RunState::queued;const auto retire=[&](RunState to,const char* reason){return store.transition(child,phase,to,Json{{"reason",reason}}.dump()).get();};
    if(cancel.stop_requested())return retire(RunState::cancelled,"cancelled_before_dynamic_tool");
    if(std::chrono::steady_clock::now()>=budget->deadline())return retire(RunState::failed,"agent_timeout");
    try{
        if(!settings.workspace)throw ToolAccessDenied("Dynamic deterministic tool requires its admitted workspace");
        WorkspaceTools workspace(*settings.workspace);
        if(workspace.identity()!=caps.workspace_identity)throw ToolAccessDenied("Dynamic deterministic workspace identity changed");
        budget->check(cancel);store.transition(child,RunState::queued,RunState::running).get();phase=RunState::running;
        const auto& definition=prepared.node.definition;
        store.append_event(child,"tool.started",Json{{"name",definition.tool},{"source","dynamic_plan"},{"plan_node_id",prepared.node.backend_node_id},{"arguments_json",definition.arguments_json}}.dump()).get();
        const auto result=Json::parse(workspace.invoke(definition.tool,definition.arguments_json,cancel));budget->check(cancel);
        if(result.dump().size()>32768)throw ToolFileError("Dynamic deterministic tool result exceeds its bound");
        store.append_event(child,"tool.completed",Json{{"name",definition.tool},{"data",result},{"source","dynamic_plan"},{"plan_node_id",prepared.node.backend_node_id}}.dump()).get();
        return store.complete_run(child,result.dump()).get();
    }catch(const ToolAccessDenied&){return retire(RunState::failed,"dynamic_workspace_precondition_unavailable");}
     catch(const ToolFileError&){return retire(RunState::failed,"dynamic_tool_execution_failed");}
     catch(const std::invalid_argument&){return retire(RunState::failed,"invalid_dynamic_tool_arguments");}
     catch(const RootBudgetExhausted&){return retire(RunState::failed,"execution_budget_exhausted");}
     catch(const ToolCancelled&){return retire(RunState::cancelled,"cancelled");}
}
Run execute_mcp_tool(PersistenceService& store,const std::string& child,const DynamicPreparedNode& prepared,
    const AgentSettings& settings,const DynamicPlanCapabilities& caps,const std::shared_ptr<RootExecutionBudget>& budget,
    std::stop_token cancel,InstructionPrecondition guidance){
    auto phase=RunState::queued;const auto retire=[&](RunState to,const char* reason){return store.transition(child,phase,to,Json{{"reason",reason}}.dump()).get();};
    if(cancel.stop_requested())return retire(RunState::cancelled,"cancelled_before_dynamic_mcp_tool");
    if(std::chrono::steady_clock::now()>=budget->deadline())return retire(RunState::failed,"agent_timeout");
    try{
        if(!settings.workspace)throw ToolAccessDenied("Dynamic MCP nodes require the admitted workspace");
        WorkspaceTools workspace(*settings.workspace);
        if(workspace.identity()!=caps.workspace_identity)throw ToolAccessDenied("Dynamic MCP workspace identity changed");
        const auto catalogue=Json::parse(mcp_compact_object(caps.tool_catalog_json));
        const auto& definition=prepared.node.definition;
        const auto frozen_tool=std::find_if(catalogue.at("tools").begin(),catalogue.at("tools").end(),[&](const auto& item){return item.is_object()&&item.value("name",std::string{})==definition.tool;});
        if(frozen_tool==catalogue.at("tools").end()||!frozen_tool->contains("mcp_binding"))throw ToolAccessDenied("Dynamic MCP binding is no longer registered");
        const auto binding=frozen_tool->at("mcp_binding");const bool readonly=binding.contains("kind")&&(binding.at("kind")=="resource"||binding.at("kind")=="prompt");
        if(!readonly&&!guidance.verify)throw ToolAccessDenied("Dynamic MCP effects require the admitted repository instruction precondition");
        const auto server_id=binding.at("server_config_id").get<std::string>();const auto revision=binding.at("config_revision").get<std::int64_t>();
        const auto server=std::find_if(settings.mcp_servers.begin(),settings.mcp_servers.end(),[&](const auto& item){return item.id==server_id&&item.revision==revision&&item.enabled;});
        if(server==settings.mcp_servers.end())throw ToolAccessDenied("Dynamic MCP server configuration changed");
        budget->check(cancel);store.transition(child,RunState::queued,RunState::running).get();phase=RunState::running;
        store.append_event(child,"tool.started",Json{{"name",definition.tool},{"source","dynamic_plan"},{"plan_node_id",prepared.node.backend_node_id},{"arguments_json",definition.arguments_json}}.dump()).get();
        std::vector<SecretBytes> private_credentials;
        auto client=connect_mcp_client(*server,store,budget->deadline(),cancel,&private_credentials);
        auto roots=admitted_workspace_roots(store,budget->root_id(),workspace);
        McpToolRegistry registry(*client,store,workspace,server_id,revision,budget->deadline(),cancel,&private_credentials,{},std::move(roots));
        const auto bindings=Json::parse(registry.approval_bindings_json());const auto actual=std::find_if(bindings.begin(),bindings.end(),[&](const auto& item){return item.is_object()&&item.value("alias",std::string{})==definition.tool;});
        if(actual==bindings.end()||*actual!=binding)throw McpCatalogueChanged("Dynamic MCP catalogue differs from its accepted binding");
        if(!readonly)guidance.validate();
        const auto expiry=now_ms()+settings.run_timeout.count();
        const auto operation=identity();
        const auto result=Json::parse(registry.invoke(operation,child,definition.tool,definition.arguments_json,expiry,budget->deadline(),cancel,readonly?InstructionPrecondition{}:std::move(guidance)));
        store.append_event(child,"tool.completed",Json{{"name",definition.tool},{"data",result},{"source","dynamic_plan"},{"plan_node_id",prepared.node.backend_node_id}}.dump()).get();
        return store.complete_run(child,result.dump()).get();
    }catch(const McpOutcomeUnrecorded&){throw;}
     catch(const McpEffectUncertain&){return retire(RunState::failed,"mcp_effect_uncertain");}
     catch(const McpReadFailed&){return retire(RunState::failed,"mcp_read_failed");}
     catch(const PermissionCancelled&){return retire(RunState::cancelled,"cancelled");}
     catch(const ToolCancelled&){return retire(RunState::cancelled,"cancelled");}
     catch(const RootBudgetExhausted&){return retire(RunState::failed,"execution_budget_exhausted");}
     catch(const ToolAccessDenied&){return retire(RunState::failed,"dynamic_workspace_precondition_unavailable");}
     catch(const std::exception&){return retire(RunState::failed,"dynamic_mcp_tool_execution_failed");}
}
Run execute_process_tool(PersistenceService& store,const std::string& child,const DynamicPreparedNode& prepared,
    const AgentSettings& settings,const DynamicPlanCapabilities& caps,const std::shared_ptr<RootExecutionBudget>& budget,
    std::stop_token cancel,InstructionPrecondition guidance){
    auto phase=RunState::queued;const auto retire=[&](RunState to,const char* reason){return store.transition(child,phase,to,Json{{"reason",reason}}.dump()).get();};
    if(cancel.stop_requested())return retire(RunState::cancelled,"cancelled_before_dynamic_process");
    if(std::chrono::steady_clock::now()>=budget->deadline())return retire(RunState::failed,"agent_timeout");
    try{
        if(!settings.workspace||settings.process_profiles.empty()||!guidance.verify)throw ToolAccessDenied("Dynamic process effects require the admitted workspace, registered profile and repository instruction precondition");
        WorkspaceTools workspace(*settings.workspace);
        if(workspace.identity()!=caps.workspace_identity)throw ToolAccessDenied("Dynamic process workspace identity changed");
        const auto& definition=prepared.node.definition;
        const auto arguments=Json::parse(mcp_compact_object(definition.arguments_json));
        if(!arguments.is_object()||arguments.value("background",false))throw std::invalid_argument("Dynamic process nodes must be synchronous");
        budget->check(cancel);store.transition(child,RunState::queued,RunState::running).get();phase=RunState::running;
        store.append_event(child,"tool.started",Json{{"name",definition.tool},{"source","dynamic_plan"},{"plan_node_id",prepared.node.backend_node_id},{"arguments_json",definition.arguments_json}}.dump()).get();
        ProcessExecutor executor(store,workspace,*settings.workspace,settings.process_profiles);
        const auto expiry=now_ms()+settings.run_timeout.count();
        const auto result=Json::parse(executor.invoke(identity(),child,definition.arguments_json,expiry,cancel,std::move(guidance),false));
        if(result.at("exit_code")!=0){
            store.append_event(child,"tool.failed",Json{{"name",definition.tool},{"data",result},{"source","dynamic_plan"},{"plan_node_id",prepared.node.backend_node_id}}.dump()).get();
            auto failure=result;failure["reason"]="process_exit_nonzero";
            return store.transition(child,RunState::running,RunState::failed,failure.dump()).get();
        }
        store.append_event(child,"tool.completed",Json{{"name",definition.tool},{"data",result},{"source","dynamic_plan"},{"plan_node_id",prepared.node.backend_node_id}}.dump()).get();
        return store.complete_run(child,result.dump()).get();
    }catch(const ProcessOutcomeUnrecorded&){throw;}
     catch(const ProcessEffectUncertain&){return retire(RunState::failed,"process_effect_uncertain");}
     catch(const PermissionCancelled&){return retire(RunState::cancelled,"cancelled");}
     catch(const PermissionDenied&){return retire(RunState::failed,"process_denied");}
     catch(const PermissionExpired&){return retire(RunState::failed,"process_approval_expired");}
     catch(const ProcessBeforeDispatchError&){return retire(RunState::failed,"process_not_dispatched");}
     catch(const ToolAccessDenied&){return retire(RunState::failed,"dynamic_workspace_precondition_unavailable");}
     catch(const std::invalid_argument&){return retire(RunState::failed,"invalid_dynamic_process_arguments");}
     catch(const RootBudgetExhausted&){return retire(RunState::failed,"execution_budget_exhausted");}
     catch(const ToolCancelled&){return retire(RunState::cancelled,"cancelled");}
     catch(const std::filesystem::filesystem_error&){return retire(RunState::failed,"dynamic_process_precondition_unavailable");}
     catch(const std::exception&){return retire(RunState::failed,"dynamic_process_execution_failed");}
}
Run execute_a2a_tool(PersistenceService& store,const std::string& child,const DynamicPreparedNode& prepared,
    const AgentSettings& settings,const DynamicPlanCapabilities& caps,const std::shared_ptr<RootExecutionBudget>& budget,
    std::stop_token cancel,InstructionPrecondition guidance){
    auto phase=RunState::queued;const auto retire=[&](RunState to,const char* reason){return store.transition(child,phase,to,Json{{"reason",reason}}.dump()).get();};
    if(cancel.stop_requested())return retire(RunState::cancelled,"cancelled_before_dynamic_a2a_tool");
    if(std::chrono::steady_clock::now()>=budget->deadline())return retire(RunState::failed,"agent_timeout");
    try{
        if(!settings.workspace||!settings.a2a_enabled||!guidance.verify)throw ToolAccessDenied("Dynamic A2A effects require the admitted workspace, enabled registration and repository instruction precondition");
        WorkspaceTools workspace(*settings.workspace);
        if(workspace.identity()!=caps.workspace_identity)throw ToolAccessDenied("Dynamic A2A workspace identity changed");
        const auto& definition=prepared.node.definition;
        const auto catalogue=Json::parse(mcp_compact_object(caps.tool_catalog_json));
        const auto frozen_tool=std::find_if(catalogue.at("tools").begin(),catalogue.at("tools").end(),[&](const auto& item){return item.is_object()&&item.value("name",std::string{})==definition.tool;});
        if(frozen_tool==catalogue.at("tools").end()||!frozen_tool->contains("a2a_binding"))throw ToolAccessDenied("Dynamic A2A binding is no longer registered");
        budget->check(cancel);store.transition(child,RunState::queued,RunState::running).get();phase=RunState::running;
        store.append_event(child,"tool.started",Json{{"name",definition.tool},{"source","dynamic_plan"},{"plan_node_id",prepared.node.backend_node_id},{"arguments_json",definition.arguments_json}}.dump()).get();
        A2aRemoteAgentRegistry registrations(store);
        const auto remaining=std::chrono::ceil<std::chrono::milliseconds>(budget->deadline()-std::chrono::steady_clock::now());
        if(remaining.count()<=0)throw RootBudgetDeadlineExceeded("Root deadline elapsed before dynamic A2A discovery");
        A2aAgentToolRegistry registry(registrations,store,workspace,std::min(remaining,std::chrono::milliseconds(5000)),cancel);
        const auto bindings=Json::parse(registry.approval_bindings_json());
        const auto actual=std::find_if(bindings.begin(),bindings.end(),[&](const auto& item){return item.is_object()&&item.value("alias",std::string{})==definition.tool;});
        if(actual==bindings.end()||*actual!=frozen_tool->at("a2a_binding"))throw A2aEffectNotDispatched("Dynamic A2A discovery differs from the accepted registration binding");
        guidance.validate();
        const auto expiry=now_ms()+settings.run_timeout.count();const auto operation=identity();
        const auto result=Json::parse(registry.invoke(operation,child,definition.tool,definition.arguments_json,expiry,budget->deadline(),cancel,std::move(guidance),[&](const std::string& event){
            budget->check(cancel);const auto observed=Json::parse(event);Json journal={{"operation_id",operation},{"plan_node_id",prepared.node.backend_node_id},{"agent_id",observed.at("agent_id")},{"data",observed.at("event")}};
            if(observed.contains("event_id")&&observed["event_id"].is_string())journal["event_id"]=observed["event_id"];
            store.append_a2a_task_event(child,operation,journal.dump()).get();
        }));
        budget->check(cancel);store.append_event(child,"tool.completed",Json{{"name",definition.tool},{"data",result},{"source","dynamic_plan"},{"plan_node_id",prepared.node.backend_node_id}}.dump()).get();
        return store.complete_run(child,result.dump()).get();
    }catch(const A2aOutcomeUnrecorded&){throw;}
     catch(const A2aEffectUncertain&){return retire(RunState::failed,"a2a_effect_uncertain");}
     catch(const A2aEffectNotDispatched&){return retire(RunState::failed,"a2a_not_dispatched");}
     catch(const PermissionCancelled&){return retire(RunState::cancelled,"cancelled");}
     catch(const PermissionDenied&){return retire(RunState::failed,"a2a_denied");}
     catch(const PermissionExpired&){return retire(RunState::failed,"a2a_approval_expired");}
     catch(const RootBudgetExhausted&){return retire(RunState::failed,"execution_budget_exhausted");}
     catch(const ToolAccessDenied&){return retire(RunState::failed,"dynamic_workspace_precondition_unavailable");}
     catch(const ToolCancelled&){return retire(RunState::cancelled,"cancelled");}
     catch(const std::exception&){return retire(RunState::failed,"dynamic_a2a_tool_execution_failed");}
}
struct ActiveBatch {
    std::unique_ptr<NativeChildExecutor::Batch> work;
    std::vector<std::string> children;
    std::vector<bool> settled;
};
std::size_t live_count(const std::vector<ActiveBatch>& active){std::size_t result=0;for(const auto& batch:active)for(const bool settled:batch.settled)if(!settled)++result;return result;}
}
AgentSettings dynamic_child_settings(const AgentSettings& parent,const DynamicPresetCapability& policy,const std::string& model){
    if(policy.revision!=1||(policy.id!="workspace.inspect"&&policy.id!="workspace.coding")||policy.readonly!=(policy.id=="workspace.inspect")||policy.turn_limit<1||policy.turn_limit>(policy.readonly?4:16)||model.empty())throw DynamicPlanUnavailable("Unsupported native dynamic child preset");
    auto child=parent;child.provider.model=model;child.selectable_models.clear();child.delegation.reset();child.max_turns=static_cast<std::size_t>(policy.turn_limit);
    if(policy.readonly){child.approved_edits=false;child.process_profiles.clear();child.mcp_servers.clear();child.a2a_enabled=false;}
    // workspace.coding deliberately inherits every enabled parent's registered
    // edit/create/process/MCP boundary. Their existing approval and fresh native
    // binding checks remain the only effect authority.
    return child;
}
DynamicPlanExecutor::DynamicPlanExecutor(PersistenceService& store,std::shared_ptr<NativeChildExecutor> children):store_(store),children_(std::move(children)){
    if(!children_)throw std::invalid_argument("Dynamic planning requires the shared native child pool");
}
bool DynamicPlanExecutor::healthy()const{return children_->healthy();}
DynamicExecutionResult DynamicPlanExecutor::invoke(const std::string& parent,const ModelToolCall& call,const std::string& assistant,const std::string& origin,
    const AgentSettings& frozen,const DynamicPlanCapabilities& caps,std::shared_ptr<RootExecutionBudget> budget,std::stop_token cancel,DynamicInstructionPrecondition guidance){
    if(!budget)throw DynamicPlanUnavailable("Dynamic execution requires its shared root budget");const auto root_id=budget->root_id();generation(store_,root_id,frozen,caps,budget,cancel,parent!=root_id);if(!healthy())throw DynamicOutcomeUnrecorded("Native child execution generation is faulted");
    const auto owner=store_.run(parent).get();const auto root=store_.run(root_id).get();if(root.graph_root||root.state!=RunState::running||owner.state!=RunState::running)throw DynamicPlanUnavailable("Dynamic plan owner is not running under its admitted root");
    std::int64_t owner_depth=0;
    if(parent==root_id){if(!owner.parent_id.empty())throw DynamicPlanUnavailable("Dynamic root owner is not an ordinary Agent");}
    else {const auto admission=store_.child_admission(parent).get();if(admission.kind!=ChildAdmissionKind::dynamic_agent||admission.root_run_id!=root_id)throw DynamicPlanUnavailable("Nested plans require an admitted dynamic-agent owner");const auto parent_plan=store_.dynamic_plan(admission.plan_id).get();if(parent_plan.state!="active"||parent_plan.root_run_id!=root_id)throw DynamicPlanUnavailable("Nested plan parent is no longer active");const auto parent_node=std::find_if(parent_plan.nodes.begin(),parent_plan.nodes.end(),[&](const auto& node){return node.child_run_id==parent;});if(parent_node==parent_plan.nodes.end())throw DynamicPlanUnavailable("Nested plan owner lost its exact parent claim");const auto& parent_preset=preset(parent_plan.capabilities,parent_node->definition);if(agent_authority_identity(frozen,frozen.provider.model,caps.workspace_identity,agent_authority_credentials(store_,frozen))!=parent_preset.backend_identity)throw DynamicPlanUnavailable("Nested plan owner settings differ from their admitted preset");owner_depth=parent_plan.depth+1;if(owner_depth>8)throw DynamicPlanRejected("planning_depth_exhausted");}
    if(call.name=="inspect_plan"){
        try{if(!Json::parse(mcp_compact_object(call.arguments_json)).empty())throw std::invalid_argument("Plan inspection accepts no model authority fields");}
        catch(const McpProtocolError&){throw DynamicPlanRejected("invalid_plan_arguments");}
        catch(const Json::exception&){throw DynamicPlanRejected("invalid_plan_arguments");}
        catch(const std::invalid_argument&){throw DynamicPlanRejected("invalid_plan_arguments");}
        const auto plan=store_.dynamic_plan_for_owner(parent).get();return {owner,{},plan?dynamic_plan_report(*plan):R"({"source":"native_dynamic_plan","plan":null})",false};
    }
    DynamicPlanCallRecord accepted;
    try{
        if(call.name!="plan_tasks"&&call.name!="revise_plan")throw std::invalid_argument("Unsupported native planning tool");
        if(call.id.empty()||origin.empty()||call.arguments_json.size()>static_cast<std::size_t>(caps.change_bytes))throw std::invalid_argument("Planning call lacks its actual bounded origin");
        // Only this preacceptance phase may become a normal rejected tool turn.
        // Runtime/settlement failures below retain their typed owner retirement.
        (void)parse_dynamic_plan_change(call.arguments_json,call.name=="plan_tasks");
        const auto existing=store_.dynamic_plan_for_owner(parent).get();
        DynamicPlanChangeSpec spec;spec.id=identity();spec.plan_id=existing?existing->id:identity();spec.root_run_id=root_id;spec.owner_run_id=parent;spec.provider_tool_call_id=call.id;
        spec.origin_attempt_id=origin;spec.arguments_json=call.arguments_json;spec.parent_assistant_json=assistant;spec.backend_identity=caps.backend_identity;spec.expected_budget_revision=budget->snapshot().revision;
        accepted=store_.accept_dynamic_plan_change(std::move(spec)).get();
    }catch(const DynamicPlanChanged&){throw DynamicPlanRejected("plan_changed");}
     catch(const RootBudgetExhausted&){throw DynamicPlanRejected("planning_budget_exhausted");}
     catch(const std::invalid_argument&){throw DynamicPlanRejected("invalid_plan_arguments");}
    if(!accepted.created){if(!accepted.result_json.empty())return {owner,accepted.id,accepted.result_json,false};throw DynamicOutcomeUnrecorded("An incomplete planning call cannot be replayed");}
    return execute(accepted,frozen,caps,std::move(budget),cancel,std::move(guidance));
}
DynamicExecutionResult DynamicPlanExecutor::resume(const std::string& parent,const std::string& id,const AgentSettings& frozen,const DynamicPlanCapabilities& caps,
    std::shared_ptr<RootExecutionBudget> budget,std::stop_token cancel,DynamicInstructionPrecondition guidance){
    if(!budget)throw DynamicPlanUnavailable("Dynamic continuation has no root budget");const auto root_id=budget->root_id();generation(store_,root_id,frozen,caps,budget,cancel,parent!=root_id);const auto accepted=store_.dynamic_plan_call(id).get();
    const auto owner=store_.run(parent).get();
    if(accepted.root_run_id!=root_id||accepted.owner_run_id!=parent||accepted.state!="accepted"||owner.state!=RunState::running)
        throw DynamicPlanUnavailable("Dynamic continuation does not own its exact resumed owner");
    if(parent==root_id){if(owner.graph_root||!owner.parent_id.empty())throw DynamicPlanUnavailable("Dynamic continuation root is not an ordinary Agent");}
    else{
        const auto admission=store_.child_admission(parent).get();const auto plan=store_.dynamic_plan(accepted.plan_id).get();
        if(!budget->may_resume_owner(parent)||admission.kind!=ChildAdmissionKind::dynamic_agent||admission.root_run_id!=root_id||
           plan.owner_run_id!=parent||plan.root_run_id!=root_id||plan.state!="active")
            throw DynamicPlanUnavailable("Nested dynamic continuation is outside the exact paused owner chain");
    }
    return execute(accepted,frozen,caps,std::move(budget),cancel,std::move(guidance));
}
DynamicExecutionResult DynamicPlanExecutor::execute(const DynamicPlanCallRecord& accepted,const AgentSettings& frozen,const DynamicPlanCapabilities& caps,
    std::shared_ptr<RootExecutionBudget> budget,std::stop_token cancel,DynamicInstructionPrecondition guidance){
    const bool nested_owner=accepted.owner_run_id!=accepted.root_run_id;
    std::vector<ActiveBatch> active;active.reserve(8);
    bool pause_observed=false;
    auto settle=[&](bool wait){
        for(auto& batch:active)for(std::size_t i=0;i<batch.children.size();++i){if(batch.settled[i])continue;const auto& result=batch.work->results()[i];
            if(!wait&&result.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)continue;
            const auto actual=result.get();if(actual.id==accepted.root_run_id&&actual.state==RunState::paused){
                (void)store_.dynamic_owner_pause(accepted.root_run_id).get();pause_observed=true;batch.settled[i]=true;continue;
            }
            if(actual.id!=batch.children[i]||!terminal(actual.state))throw DynamicOutcomeUnrecorded("Dynamic child returned without its exact owned retirement");
            try{store_.settle_dynamic_child(actual.id).get();}
            catch(...){children_->fail();throw DynamicOutcomeUnrecorded("Dynamic child outcome could not be journaled; recovery is required");}
            batch.settled[i]=true;
        }
    };
    try{
        for(;;){
            settle(false);if(pause_observed){settle(true);const auto paused=store_.run(accepted.root_run_id).get();if(paused.state!=RunState::paused)throw DynamicOutcomeUnrecorded("Nested human pause lost its root owner state");return {paused,accepted.id,{},true};}
            budget->check(cancel);if(!healthy())throw DynamicOutcomeUnrecorded("Native child execution generation is faulted");
            auto plan=store_.dynamic_plan(accepted.plan_id).get();
            if(plan.root_run_id!=accepted.root_run_id||plan.revision!=accepted.accepted_revision)throw DynamicOutcomeUnrecorded("Outstanding planning call lost its immutable accepted revision");
            for(const auto& request:store_.dynamic_human_requests(plan.id).get()){
                if(request.state=="expired")throw DynamicHumanExpired("An owned human question expired; no provider continuation is dispatched");
                if(request.state=="waiting"&&request.expires_unix_ms<=now_ms()){
                    const auto actual=store_.expire_dynamic_human(plan.id,request.id).get();
                    if(actual.state=="expired")throw DynamicHumanExpired("An owned human question expired; no provider continuation is dispatched");
                    plan=store_.dynamic_plan(plan.id).get();
                }
            }
            const auto decision=inspect_dynamic_plan(plan);
            if(decision.halted)throw WorkspaceEffectUncertain("Dynamic plan retains an uncertain owned outcome");
            std::vector<DynamicPreparedNode> resumable;
            for(const auto& label:decision.claimed){const auto found=std::find_if(plan.nodes.begin(),plan.nodes.end(),[&](const auto& node){return node.definition.label==label;});
                if(found!=plan.nodes.end()&&found->definition.kind==DynamicNodeKind::agent&&budget->may_resume_owner(found->child_run_id))resumable.push_back({*found,found->resolved_input_json});}
            if(decision.claimed.size()>live_count(active)+resumable.size())throw DynamicOutcomeUnrecorded("Dynamic plan contains an untracked claim; live work cannot be adopted or replayed");
            if(!resumable.empty()&&live_count(active)==0){
                std::vector<NativeChildWork> work;ActiveBatch staged;work.reserve(resumable.size());staged.children.reserve(resumable.size());staged.settled.assign(resumable.size(),false);
                for(const auto& prepared:resumable){const auto child_id=prepared.node.child_run_id;const auto& policy=preset(plan.capabilities,prepared.node.definition);
                    auto settings=dynamic_child_settings(frozen,policy,selected_model(plan.capabilities));const auto actual=store_.run(child_id).get();
                    if(actual.state!=RunState::running||store_.child_admission(child_id).get().claim_id!=prepared.node.claim_id)throw DynamicOutcomeUnrecorded("Paused dynamic child claim is not the exact resumed owner");
                    staged.children.push_back(child_id);
                    work.push_back({budget,[this,child_id,settings=std::move(settings),policy,caps,budget,prepared,guidance](std::stop_token stop)mutable{
                        bool available=false;try{available=agent_authority_identity(settings,settings.provider.model,caps.workspace_identity,agent_authority_credentials(store_,settings))==policy.backend_identity;}
                        catch(const Conflict&){}catch(const NotFound&){}
                        if(!available)return store_.transition(child_id,RunState::running,RunState::failed,R"({"reason":"dynamic_preset_unavailable_after_resume"})").get();
                        std::unique_ptr<AgentRunner> runner;
                        try{auto nested_planning=settings.planning?std::make_shared<DynamicPlanExecutor>(store_,children_):std::shared_ptr<DynamicPlanExecutor>{};runner=std::make_unique<AgentRunner>(store_,settings,std::shared_ptr<DelegationExecutor>{},std::move(nested_planning),true);}
                        catch(const ProcessBeforeDispatchError&){return store_.transition(child_id,RunState::running,RunState::failed,R"({"reason":"dynamic_process_precondition_unavailable"})").get();}
                        catch(const ToolAccessDenied&){return store_.transition(child_id,RunState::running,RunState::failed,R"({"reason":"dynamic_workspace_precondition_unavailable"})").get();}
                        catch(const ToolFileError&){return store_.transition(child_id,RunState::running,RunState::failed,R"({"reason":"dynamic_workspace_precondition_unavailable"})").get();}
                        catch(const std::filesystem::filesystem_error&){return store_.transition(child_id,RunState::running,RunState::failed,R"({"reason":"dynamic_runtime_path_unavailable"})").get();}
                        catch(const std::invalid_argument&){if(!changed_process_precondition(settings))throw;return store_.transition(child_id,RunState::running,RunState::failed,R"({"reason":"dynamic_process_precondition_unavailable"})").get();}
                        return runner->execute(child_id,stop,{},budget,{}, {},true);
                    },cancel});
                }
                try{staged.work=children_->stage(std::move(work));}
                catch(const NativeChildCapacityUnavailable&){std::this_thread::sleep_for(std::chrono::milliseconds(10));continue;}
                catch(const NativeChildOutcomeUnrecorded&){throw DynamicOutcomeUnrecorded("Native child execution generation is faulted");}
                active.push_back(std::move(staged));active.back().work->dispatch();continue;
            }
            if(decision.report_ready){
                generation(store_,accepted.root_run_id,frozen,caps,budget,cancel,nested_owner);
                DynamicPlanStepResult result;
                try{result=store_.settle_dynamic_plan_step(accepted.id).get();}
                catch(const WorkspaceEffectUncertain&){throw;}
                catch(...){children_->fail();throw DynamicOutcomeUnrecorded("Dynamic joined report could not be journaled; recovery is required");}
                if(!result.ready)continue;
                if(result.call.result_json.empty())throw DynamicOutcomeUnrecorded("Dynamic joined report has no actual owned result");
                return {store_.run(accepted.owner_run_id).get(),accepted.id,result.call.result_json,false};
            }
            bool published=false;
            for(const auto& id:decision.ready){const auto prepared=prepare_dynamic_node(plan,id);if(prepared.node.definition.kind!=DynamicNodeKind::human)continue;
                generation(store_,accepted.root_run_id,frozen,caps,budget,cancel,nested_owner);
                DynamicHumanRequestSpec spec;spec.id=identity();spec.plan_id=plan.id;spec.label=id;spec.backend_identity=caps.backend_identity;spec.plan_call_id=accepted.id;
                spec.expected_revision=plan.revision;spec.expected_state_sequence=plan.state_sequence;spec.expires_unix_ms=now_ms()+caps.human_expiry_ms;
                try{store_.publish_dynamic_human(std::move(spec)).get();}catch(const DynamicPlanChanged&){published=true;break;}
                published=true;break;
            }
            if(published)continue;
            const auto live=live_count(active);const auto parallel=static_cast<std::size_t>(budget->snapshot().spec.max_parallel);std::vector<DynamicPreparedNode> frontier;
            for(const auto& id:decision.ready){if(frontier.size()+live>=parallel)break;auto prepared=prepare_dynamic_node(plan,id);if(prepared.node.definition.kind==DynamicNodeKind::agent||prepared.node.definition.kind==DynamicNodeKind::tool)frontier.push_back(std::move(prepared));}
            if(!frontier.empty()){
                generation(store_,accepted.root_run_id,frozen,caps,budget,cancel,nested_owner);
                DynamicFrontierSpec claim;claim.plan_id=plan.id;claim.backend_identity=caps.backend_identity;claim.plan_call_id=accepted.id;
                claim.expected_revision=plan.revision;claim.expected_state_sequence=plan.state_sequence;claim.expected_budget_revision=budget->snapshot().revision;
                std::vector<NativeChildWork> work;work.reserve(frontier.size());ActiveBatch staged;staged.children.reserve(frontier.size());staged.settled.assign(frontier.size(),false);
                for(const auto& prepared:frontier){const auto child_id=identity();AgentSettings settings;DynamicPresetCapability policy;
                    if(prepared.node.definition.kind==DynamicNodeKind::agent){policy=preset(caps,prepared.node.definition);settings=dynamic_child_settings(frozen,policy,selected_model(caps));}
                    else settings=frozen;
                    DynamicTaskClaimSpec task;task.label=prepared.node.definition.label;task.claim_id=identity();task.child_run_id=child_id;task.prompt_json=(prepared.node.definition.kind==DynamicNodeKind::tool?deterministic_prompt(prepared):prompt(prepared,caps)).dump();task.resolved_input_json=prepared.dependency_outputs_json;task.definition_revision=prepared.node.definition_revision;claim.tasks.push_back(std::move(task));staged.children.push_back(child_id);
                    work.push_back({budget,[this,child_id,settings=std::move(settings),policy,caps,budget,prepared,guidance](std::stop_token stop)mutable{
                        if(prepared.node.definition.kind==DynamicNodeKind::tool){
                            static const std::set<std::string> readonly={"read_repository_instructions","read_file","list_files","search_files","find_files","glob_files"};
                            if(readonly.contains(prepared.node.definition.tool))return execute_readonly_tool(store_,child_id,prepared,settings,caps,budget,stop);
                            if(prepared.node.definition.tool=="run_process"){
                                const auto arguments=Json::parse(mcp_compact_object(prepared.node.definition.arguments_json));const auto directory=arguments.value("workdir",std::string{"."});
                                auto precondition=guidance?guidance(directory,stop):InstructionPrecondition{};
                                return execute_process_tool(store_,child_id,prepared,settings,caps,budget,stop,std::move(precondition));
                            }
                            auto precondition=guidance?guidance(".",stop):InstructionPrecondition{};
                            if(prepared.node.definition.tool.starts_with("a2a_delegate_"))return execute_a2a_tool(store_,child_id,prepared,settings,caps,budget,stop,std::move(precondition));
                            return execute_mcp_tool(store_,child_id,prepared,settings,caps,budget,stop,std::move(precondition));
                        }
                        const auto retire_before_launch=[&]()->std::optional<Run>{
                            if(stop.stop_requested())return store_.transition(child_id,RunState::queued,RunState::cancelled,R"({"reason":"cancelled_before_dynamic_child"})").get();
                            if(std::chrono::steady_clock::now()>=budget->deadline())return store_.transition(child_id,RunState::queued,RunState::failed,R"({"reason":"agent_timeout"})").get();return {};};
                        if(auto retired=retire_before_launch())return *retired;
                        bool available=false;
                        try{available=agent_authority_identity(settings,settings.provider.model,caps.workspace_identity,agent_authority_credentials(store_,settings))==policy.backend_identity;}
                        catch(const Conflict&){available=false;}
                        catch(const NotFound&){available=false;}
                        if(!available)return store_.transition(child_id,RunState::queued,RunState::failed,R"({"reason":"dynamic_preset_unavailable"})").get();
                        if(auto retired=retire_before_launch())return *retired;
                        std::unique_ptr<AgentRunner> runner;
                        try{auto nested_planning=settings.planning?std::make_shared<DynamicPlanExecutor>(store_,children_):std::shared_ptr<DynamicPlanExecutor>{};runner=std::make_unique<AgentRunner>(store_,settings,std::shared_ptr<DelegationExecutor>{},std::move(nested_planning),true);}
                        catch(const ProcessBeforeDispatchError&){return store_.transition(child_id,RunState::queued,RunState::failed,R"({"reason":"dynamic_process_precondition_unavailable"})").get();}
                        catch(const ToolAccessDenied&){return store_.transition(child_id,RunState::queued,RunState::failed,R"({"reason":"dynamic_workspace_precondition_unavailable"})").get();}
                        catch(const ToolFileError&){return store_.transition(child_id,RunState::queued,RunState::failed,R"({"reason":"dynamic_workspace_precondition_unavailable"})").get();}
                        catch(const std::filesystem::filesystem_error&){return store_.transition(child_id,RunState::queued,RunState::failed,R"({"reason":"dynamic_runtime_path_unavailable"})").get();}
                        catch(const std::invalid_argument&){
                            // ProcessExecutor's missing/nonregular executable
                            // check predates its typed dispatch precondition.
                            // Prove that frozen binding unavailable before
                            // treating this as an expected no-dispatch failure.
                            if(!changed_process_precondition(settings))throw;
                            return store_.transition(child_id,RunState::queued,RunState::failed,R"({"reason":"dynamic_process_precondition_unavailable"})").get();
                        }
                        return runner->execute(child_id,stop,{},budget);
                    },cancel});
                }
                try{staged.work=children_->stage(std::move(work));}
                catch(const NativeChildCapacityUnavailable&){std::this_thread::sleep_for(std::chrono::milliseconds(10));continue;}
                catch(const NativeChildOutcomeUnrecorded&){throw DynamicOutcomeUnrecorded("Native child execution generation is faulted");}
                DynamicFrontierClaim committed;
                try{budget->check(cancel);committed=store_.admit_dynamic_frontier(std::move(claim)).get();}
                catch(const DynamicPlanChanged&){continue;}
                if(!committed.created||committed.nodes.size()!=staged.children.size())throw DynamicOutcomeUnrecorded("Dynamic child claim ownership changed before dispatch");
                for(std::size_t i=0;i<staged.children.size();++i)if(committed.nodes[i].child_run_id!=staged.children[i])throw DynamicOutcomeUnrecorded("Dynamic child claim differs from its staged native owner");
                active.push_back(std::move(staged));active.back().work->dispatch();continue;
            }
            if(live==0&&!decision.waiting_human.empty()&&decision.ready.empty()){
                generation(store_,accepted.root_run_id,frozen,caps,budget,cancel,nested_owner);
                try{budget->wait_for_leaves_drained(cancel);const auto paused=budget->suspend_for_human(plan,accepted.id);return {paused,accepted.id,{},true};}
                catch(const DynamicPlanChanged&){continue;}
            }
            if(live==0&&decision.waiting_human.empty())throw DynamicOutcomeUnrecorded("Dynamic plan has no owned progress, report or human wait");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }catch(...){
        const auto original=std::current_exception();for(auto& batch:active)batch.work->request_stop();
        bool journal_fault=false;
        // Drain EVERY actual future even when one worker or settlement fails.
        // Completed effects/history remain under their original child owner.
        for(auto& batch:active)for(std::size_t i=0;i<batch.children.size();++i){if(batch.settled[i])continue;
            try{const auto actual=batch.work->results()[i].get();if(actual.id!=batch.children[i]||!terminal(actual.state))throw DynamicOutcomeUnrecorded("Dynamic child failed to retire");store_.settle_dynamic_child(actual.id).get();batch.settled[i]=true;}
            catch(...){journal_fault=true;children_->fail();}
        }
        if(journal_fault)throw DynamicOutcomeUnrecorded("Dynamic children drained but outcomes require recovery; execution must stop");
        std::rethrow_exception(original);
    }
}
}
