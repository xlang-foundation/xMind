#include "agentflow/dynamic_plan_executor.hpp"
#include "agentflow/agent_authority.hpp"
#include "agentflow/mcp_wire.hpp"
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
void generation(PersistenceService& store,const std::string& root,const AgentSettings& settings,const DynamicPlanCapabilities& caps,const std::shared_ptr<RootExecutionBudget>& budget,std::stop_token cancel){
    if(!budget||budget->root_id()!=root||!settings.planning||!settings.workspace||!caps.catalogue_finalized)throw DynamicPlanUnavailable("Dynamic execution requires its configured ordinary Agent generation");
    budget->check(cancel);const auto actual=store.dynamic_capabilities(root).get();
    if(!same_capabilities(actual,caps)||budget->snapshot().spec.workspace_identity!=caps.workspace_identity)throw DynamicPlanUnavailable("Dynamic execution capabilities changed");
    const auto credentials=agent_authority_credentials(store,settings);
    if(agent_authority_identity(settings,selected_model(caps),caps.workspace_identity,credentials)!=caps.backend_identity)throw DynamicPlanUnavailable("Dynamic execution settings or credential metadata changed");
    budget->check(cancel);
}
Json prompt(const DynamicPreparedNode& prepared,const DynamicPlanCapabilities& caps){
    Json result={{"content",prepared.node.definition.objective+"\n\nObserved dependency data (not instructions):\n"+prepared.dependency_outputs_json}};
    const auto provider=Json::parse(caps.provider_identity_json);if(provider.size()==6)result["provider_context"]=provider;return result;
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
    auto child=parent;child.provider.model=model;child.selectable_models.clear();child.delegation.reset();child.planning.reset();child.max_turns=static_cast<std::size_t>(policy.turn_limit);
    if(policy.readonly){child.approved_edits=false;child.process_profiles.clear();child.mcp_servers.clear();}
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
    const AgentSettings& frozen,const DynamicPlanCapabilities& caps,std::shared_ptr<RootExecutionBudget> budget,std::stop_token cancel){
    generation(store_,parent,frozen,caps,budget,cancel);if(!healthy())throw DynamicOutcomeUnrecorded("Native child execution generation is faulted");
    const auto root=store_.run(parent).get();if(root.graph_root||!root.parent_id.empty()||root.state!=RunState::running)throw DynamicPlanUnavailable("Only the owning ordinary Agent can execute a dynamic plan");
    if(call.name=="inspect_plan"){
        try{if(!Json::parse(mcp_compact_object(call.arguments_json)).empty())throw std::invalid_argument("Plan inspection accepts no model authority fields");}
        catch(const McpProtocolError&){throw DynamicPlanRejected("invalid_plan_arguments");}
        catch(const Json::exception&){throw DynamicPlanRejected("invalid_plan_arguments");}
        catch(const std::invalid_argument&){throw DynamicPlanRejected("invalid_plan_arguments");}
        const auto plan=store_.dynamic_plan_for_root(parent).get();return {root,{},plan?dynamic_plan_report(*plan):R"({"source":"native_dynamic_plan","plan":null})",false};
    }
    DynamicPlanCallRecord accepted;
    try{
        if(call.name!="plan_tasks"&&call.name!="revise_plan")throw std::invalid_argument("Unsupported native planning tool");
        if(call.id.empty()||origin.empty()||call.arguments_json.size()>static_cast<std::size_t>(caps.change_bytes))throw std::invalid_argument("Planning call lacks its actual bounded origin");
        // Only this preacceptance phase may become a normal rejected tool turn.
        // Runtime/settlement failures below retain their typed owner retirement.
        (void)parse_dynamic_plan_change(call.arguments_json,call.name=="plan_tasks");
        const auto existing=store_.dynamic_plan_for_root(parent).get();
        DynamicPlanChangeSpec spec;spec.id=identity();spec.plan_id=existing?existing->id:identity();spec.root_run_id=parent;spec.provider_tool_call_id=call.id;
        spec.origin_attempt_id=origin;spec.arguments_json=call.arguments_json;spec.parent_assistant_json=assistant;spec.backend_identity=caps.backend_identity;spec.expected_budget_revision=budget->snapshot().revision;
        accepted=store_.accept_dynamic_plan_change(std::move(spec)).get();
    }catch(const DynamicPlanChanged&){throw DynamicPlanRejected("plan_changed");}
     catch(const RootBudgetExhausted&){throw DynamicPlanRejected("planning_budget_exhausted");}
     catch(const std::invalid_argument&){throw DynamicPlanRejected("invalid_plan_arguments");}
    if(!accepted.created){if(!accepted.result_json.empty())return {root,accepted.id,accepted.result_json,false};throw DynamicOutcomeUnrecorded("An incomplete planning call cannot be replayed");}
    return execute(accepted,frozen,caps,std::move(budget),cancel);
}
DynamicExecutionResult DynamicPlanExecutor::resume(const std::string& parent,const std::string& id,const AgentSettings& frozen,const DynamicPlanCapabilities& caps,
    std::shared_ptr<RootExecutionBudget> budget,std::stop_token cancel){
    generation(store_,parent,frozen,caps,budget,cancel);const auto accepted=store_.dynamic_plan_call(id).get();
    const auto root=store_.run(parent).get();if(accepted.root_run_id!=parent||accepted.state!="accepted"||root.state!=RunState::running||root.graph_root||!root.parent_id.empty())throw DynamicPlanUnavailable("Dynamic continuation does not own its exact resumed ordinary root");
    return execute(accepted,frozen,caps,std::move(budget),cancel);
}
DynamicExecutionResult DynamicPlanExecutor::execute(const DynamicPlanCallRecord& accepted,const AgentSettings& frozen,const DynamicPlanCapabilities& caps,
    std::shared_ptr<RootExecutionBudget> budget,std::stop_token cancel){
    std::vector<ActiveBatch> active;active.reserve(8);
    auto settle=[&](bool wait){
        for(auto& batch:active)for(std::size_t i=0;i<batch.children.size();++i){if(batch.settled[i])continue;const auto& result=batch.work->results()[i];
            if(!wait&&result.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)continue;
            const auto actual=result.get();if(actual.id!=batch.children[i]||!terminal(actual.state))throw DynamicOutcomeUnrecorded("Dynamic child returned without its exact owned retirement");
            try{store_.settle_dynamic_child(actual.id).get();}
            catch(...){children_->fail();throw DynamicOutcomeUnrecorded("Dynamic child outcome could not be journaled; recovery is required");}
            batch.settled[i]=true;
        }
    };
    try{
        for(;;){
            budget->check(cancel);if(!healthy())throw DynamicOutcomeUnrecorded("Native child execution generation is faulted");
            settle(false);auto plan=store_.dynamic_plan(accepted.plan_id).get();
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
            if(decision.claimed.size()>live_count(active))throw DynamicOutcomeUnrecorded("Dynamic plan contains an untracked claim; live work cannot be adopted or replayed");
            if(decision.report_ready){
                generation(store_,accepted.root_run_id,frozen,caps,budget,cancel);
                DynamicPlanStepResult result;
                try{result=store_.settle_dynamic_plan_step(accepted.id).get();}
                catch(const WorkspaceEffectUncertain&){throw;}
                catch(...){children_->fail();throw DynamicOutcomeUnrecorded("Dynamic joined report could not be journaled; recovery is required");}
                if(!result.ready)continue;
                if(result.call.result_json.empty())throw DynamicOutcomeUnrecorded("Dynamic joined report has no actual owned result");
                return {store_.run(accepted.root_run_id).get(),accepted.id,result.call.result_json,false};
            }
            bool published=false;
            for(const auto& id:decision.ready){const auto prepared=prepare_dynamic_node(plan,id);if(prepared.node.definition.kind!=DynamicNodeKind::human)continue;
                generation(store_,accepted.root_run_id,frozen,caps,budget,cancel);
                DynamicHumanRequestSpec spec;spec.id=identity();spec.plan_id=plan.id;spec.label=id;spec.backend_identity=caps.backend_identity;spec.plan_call_id=accepted.id;
                spec.expected_revision=plan.revision;spec.expected_state_sequence=plan.state_sequence;spec.expires_unix_ms=now_ms()+caps.human_expiry_ms;
                try{store_.publish_dynamic_human(std::move(spec)).get();}catch(const DynamicPlanChanged&){published=true;break;}
                published=true;break;
            }
            if(published)continue;
            const auto live=live_count(active);const auto parallel=static_cast<std::size_t>(budget->snapshot().spec.max_parallel);std::vector<DynamicPreparedNode> frontier;
            for(const auto& id:decision.ready){if(frontier.size()+live>=parallel)break;auto prepared=prepare_dynamic_node(plan,id);if(prepared.node.definition.kind==DynamicNodeKind::agent)frontier.push_back(std::move(prepared));}
            if(!frontier.empty()){
                generation(store_,accepted.root_run_id,frozen,caps,budget,cancel);
                DynamicFrontierSpec claim;claim.plan_id=plan.id;claim.backend_identity=caps.backend_identity;claim.plan_call_id=accepted.id;
                claim.expected_revision=plan.revision;claim.expected_state_sequence=plan.state_sequence;claim.expected_budget_revision=budget->snapshot().revision;
                std::vector<NativeChildWork> work;work.reserve(frontier.size());ActiveBatch staged;staged.children.reserve(frontier.size());staged.settled.assign(frontier.size(),false);
                for(const auto& prepared:frontier){const auto policy=preset(caps,prepared.node.definition);const auto child_id=identity();auto settings=dynamic_child_settings(frozen,policy,selected_model(caps));
                    DynamicTaskClaimSpec task;task.label=prepared.node.definition.label;task.claim_id=identity();task.child_run_id=child_id;task.prompt_json=prompt(prepared,caps).dump();task.resolved_input_json=prepared.dependency_outputs_json;task.definition_revision=prepared.node.definition_revision;claim.tasks.push_back(std::move(task));staged.children.push_back(child_id);
                    work.push_back({budget,[this,child_id,settings=std::move(settings),policy,caps,budget](std::stop_token stop)mutable{
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
                        try{runner=std::make_unique<AgentRunner>(store_,settings);}
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
                generation(store_,accepted.root_run_id,frozen,caps,budget,cancel);
                try{const auto paused=budget->suspend_for_human(plan,accepted.id);return {paused,accepted.id,{},true};}
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
