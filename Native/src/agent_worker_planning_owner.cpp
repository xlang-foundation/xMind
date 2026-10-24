#include "agentflow/agent_worker_planning_owner.hpp"
#include "agentflow/agent_runner.hpp"
#include "agentflow/agent_authority.hpp"
#include "agentflow/root_execution_budget.hpp"
#include "agentflow/agent_worker_context_window.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include "agentflow/native_child_executor.hpp"
#include "agentflow/dynamic_plan_executor.hpp"

namespace agentflow {
namespace {
using Json=nlohmann::json;
struct Owner {
    PersistenceService& store;
    AgentWorkerLeaseSpec lease;
    AgentSettings settings;
    DynamicPlanCapabilities capabilities;
    std::shared_ptr<DynamicPlanExecution> execution;
    std::shared_ptr<RootExecutionBudget> budget;
    AgentWorkerChannel::Deadline deadline;
    std::stop_token cancel;
    DynamicInstructionPrecondition guidance;
    std::shared_ptr<NativeChildSlot> slot;
    template<class Action> DynamicExecutionResult execute_with_slot(Action action,std::stop_token stop){
        if(!slot)return action();
        if(!slot->yield())throw DynamicOutcomeUnrecorded("Planning owner lost its exact child slot");
        DynamicExecutionResult result;try{result=action();}catch(...){const auto failure=std::current_exception();slot->reacquire(stop);std::rethrow_exception(failure);}
        if(!result.paused)slot->reacquire(stop);return result;
    }

    DynamicExecutionResult owner_result(DynamicExecutionResult result){
        if(result.paused&&lease.owner_run_id!=lease.root_run_id){
            if(result.root.id!=lease.root_run_id||result.root.state!=RunState::paused||!result.root.parent_id.empty()||result.root.graph_root)throw DynamicOutcomeUnrecorded("Nested pause lost its ordinary root");
            const auto chain=store.dynamic_owner_pause(lease.root_run_id).get();
            if(std::find(chain.owner_runs.begin(),chain.owner_runs.end(),lease.owner_run_id)==chain.owner_runs.end())throw DynamicOutcomeUnrecorded("Nested pause lost its admitted owner chain");
            result.root=store.run(lease.owner_run_id).get();
            if(result.root.state!=RunState::paused||result.root.parent_id!=lease.root_run_id||result.root.graph_root)throw DynamicOutcomeUnrecorded("Nested pause lost its exact child state");
        }
        return result;
    }
    void check(std::stop_token stop={})const{
        if(cancel.stop_requested()||stop.stop_requested())throw TransportCancelled("Planning owner cancelled");
        if(std::chrono::steady_clock::now()>=deadline)throw TransportTimeout("Planning owner expired");
        if((slot&&(lease.root_run_id==lease.owner_run_id||!slot->shares_budget(budget)))||!execution||!budget||!settings.planning||!settings.workspace||budget->root_id()!=lease.root_run_id||budget->deadline()!=deadline||!budget->segment_open())throw DynamicPlanUnavailable("Planning requires its captured dynamic owner");
        budget->check(stop);
        (void)store.read_agent_worker_journal(lease,{AgentWorkerJournalReadKind::run,lease.owner_run_id}).get();
        const auto segment=store.dynamic_budget_segment(lease.root_run_id).get();
        auto owner_authority=capabilities.backend_identity;
        if(lease.owner_run_id!=lease.root_run_id){
            const auto admitted=store.child_admission(lease.owner_run_id).get();
            const auto parent=store.dynamic_plan(admitted.plan_id).get();
            const auto preset=std::find_if(parent.capabilities.presets.begin(),parent.capabilities.presets.end(),[&](const auto& value){return value.id==admitted.preset_id&&value.revision==admitted.preset_revision;});
            if(admitted.kind!=ChildAdmissionKind::dynamic_agent||admitted.root_run_id!=lease.root_run_id||parent.root_run_id!=lease.root_run_id||preset==parent.capabilities.presets.end()||preset->backend_identity!=admitted.backend_identity)throw DynamicPlanUnavailable("Planning child preset authority changed");
            owner_authority=preset->backend_identity;
        }
        if(segment.state!="open"||segment.id!=budget->segment_id()||!capabilities.catalogue_finalized||capabilities.workspace_identity!=lease.workspace_id||Json::parse(capabilities.provider_identity_json)!=Json::parse(lease.provider_identity_json)||agent_authority_identity(settings,settings.provider.model,capabilities.workspace_identity,agent_authority_credentials(store,settings))!=owner_authority)throw DynamicPlanUnavailable("Planning owner authority changed");
        if(!execution->healthy())throw DynamicOutcomeUnrecorded("Planning execution generation is faulted");
    }
    void started(const AgentWorkerPlanningClaimRecord& claim){
        const auto activity=lease.owner_run_id+":plan:"+claim.call_id;unsigned found=0;
        for(const auto& event:store.events(lease.owner_run_id).get())if(event.kind=="tool.started"){
            const auto value=Json::parse(event.json);if(value.value("activity_id",std::string{})!=activity)continue;
            if(++found!=1||value.value("call_id",std::string{})!=claim.call_id||value.value("name",std::string{})!=claim.tool_name||(value.contains("arguments_json")?value.at("arguments_json")!=claim.arguments_json:(!value.contains("arguments")||value.at("arguments")!=Json::parse(claim.arguments_json))))throw Conflict("Planning start differs from the actual model call");
        }
        if(!found)store.append_event(lease.owner_run_id,"tool.started",Json{{"activity_id",activity},{"call_id",claim.call_id},{"name",claim.tool_name},{"arguments_json",claim.arguments_json}}.dump()).get();
    }
    DynamicExecutionResult invoke(const std::string& attempt,const std::string& call,std::stop_token stop){
        std::stop_source linked;std::stop_callback owner_stop(cancel,[&]{linked.request_stop();});std::stop_callback caller_stop(stop,[&]{linked.request_stop();});check(linked.get_token());
        const auto claim=store.claim_agent_worker_planning(lease,attempt,call,deadline,cancel,linked.get_token()).get();
        const auto model=store.agent_worker_model_claim(lease.root_run_id,attempt).get();
        started(claim);
        try{
            if(claim.tool_name=="inspect_plan"){
                if(!Json::parse(claim.arguments_json).empty())throw DynamicPlanRejected("invalid_plan_arguments");
                const auto report=store.commit_agent_worker_plan_inspection(lease,attempt,deadline,cancel,linked.get_token()).get();
                return {store.run(lease.owner_run_id).get(),{},report,false};
            }
            return owner_result(execute_with_slot([&]{return execution->invoke(lease.owner_run_id,{claim.call_id,claim.tool_name,claim.arguments_json},model.actual_assistant_json,attempt,settings,capabilities,budget,linked.get_token(),guidance);},linked.get_token()));
        }catch(const DynamicPlanRejected& error){
            try{store.record_rejected_dynamic_tool_turn(lease.owner_run_id,attempt,model.actual_assistant_json,error.code).get();}
            catch(...){throw DynamicOutcomeUnrecorded("Planning rejection was not durably committed");}
            throw;
        }
    }
    DynamicExecutionResult resume(const std::string& call,std::stop_token stop){
        std::stop_source linked;std::stop_callback owner_stop(cancel,[&]{linked.request_stop();});std::stop_callback caller_stop(stop,[&]{linked.request_stop();});check(linked.get_token());
        const auto accepted=store.dynamic_plan_call(call).get();
        auto claim=store.agent_worker_planning_claim(lease.root_run_id,accepted.origin_attempt_id).get();
        if(accepted.root_run_id!=lease.root_run_id||accepted.owner_run_id!=lease.owner_run_id||claim.state!="accepted"||claim.native_call_id!=call)throw DynamicPlanUnavailable("Planning resume differs from its captured accepted call");
        if(claim.model.generation!=lease.generation)claim=store.adopt_agent_worker_planning(lease,call).get();
        else if(claim.segment_id!=budget->segment_id())throw DynamicPlanUnavailable("Planning resume lost its original segment");
        return owner_result(execute_with_slot([&]{return execution->resume(lease.owner_run_id,call,settings,capabilities,budget,linked.get_token(),guidance);},linked.get_token()));
    }
};
NativePlanningCallbacks callbacks_for(std::shared_ptr<Owner> owner){
    NativePlanningCallbacks result;const auto& captured=owner->lease;result.scope={captured.root_run_id,captured.owner_run_id,captured.workspace_id,captured.generation,captured.workspace_revision};
    result.invoke=[owner](const auto& attempt,const auto& call,std::stop_token stop){return owner->invoke(attempt,call,stop);};
    result.resume=[owner](const auto& call,std::stop_token stop){return owner->resume(call,stop);};
    result.healthy=[owner]{try{owner->check();return true;}catch(...){return false;}};
    result.pending=[owner]{owner->check();std::string pending;for(const auto& call:owner->store.dynamic_plan_calls(owner->lease.root_run_id).get()){if(call.owner_run_id!=owner->lease.owner_run_id)continue;if(call.state=="accepted"){if(!pending.empty())throw Conflict("Planning owner has multiple accepted continuations");pending=call.id;}else if(call.state=="report_ready"||(call.state=="turn_committed"&&call.continuation_attempt_id.empty()))throw DynamicPlanUnavailable("Pending planning report requires separate ownership recovery");}return pending;};
    result.with_owner_guidance=[owner](DynamicInstructionPrecondition guidance){if(!guidance)throw Conflict("Planning guidance binding requires a native owner");auto bound=std::make_shared<Owner>(*owner);bound->guidance=std::move(guidance);return callbacks_for(std::move(bound));};
    return result;
}
}
NativePlanningCallbacks make_agent_worker_planning_callbacks(PersistenceService& store,AgentWorkerLeaseSpec lease,AgentSettings settings,
    std::shared_ptr<DynamicPlanExecution> execution,std::shared_ptr<RootExecutionBudget> budget,
    AgentWorkerChannel::Deadline deadline,std::stop_token cancel,DynamicInstructionPrecondition guidance,std::shared_ptr<NativeChildSlot> slot){
    if(slot)if(auto native=std::dynamic_pointer_cast<DynamicPlanExecutor>(execution))execution=native->for_child(slot,budget);
    auto owner=std::make_shared<Owner>(Owner{store,std::move(lease),std::move(settings),{},std::move(execution),std::move(budget),deadline,cancel,std::move(guidance),std::move(slot)});
    owner->capabilities=store.dynamic_capabilities(owner->lease.root_run_id).get();owner->check();
    return callbacks_for(std::move(owner));
}
NativePlanningCallbacks bind_agent_worker_planning_context(NativePlanningCallbacks callbacks,
    PersistenceService& store,AgentWorkerLeaseSpec lease,AgentWorkerContextWindow* window,DynamicInstructionPrecondition guidance){
    const AgentWorkerScope expected{lease.root_run_id,lease.owner_run_id,lease.workspace_id,lease.generation,lease.workspace_revision};
    if(callbacks.scope!=expected||!callbacks.invoke||!callbacks.resume||!callbacks.healthy)throw Conflict("Planning callbacks differ from the context owner");
    if(callbacks.with_owner_guidance&&guidance){callbacks=callbacks.with_owner_guidance(std::move(guidance));if(callbacks.scope!=expected||!callbacks.invoke||!callbacks.resume||!callbacks.healthy)throw Conflict("Bound planning guidance changed its owner scope");}
    auto complete=[&store,lease=std::move(lease),window](DynamicExecutionResult result,std::stop_token stop){
        if(!result.paused&&!result.call_id.empty()){
            store.commit_dynamic_tool_turn(lease.owner_run_id,result.call_id).get();
            if(window)(void)window->prepare_planning_continuation(result.call_id,stop);
        }
        return result;
    };
    callbacks.invoke=[invoke=std::move(callbacks.invoke),complete](const auto& attempt,const auto& call,std::stop_token stop){return complete(invoke(attempt,call,stop),stop);};
    callbacks.resume=[resume=std::move(callbacks.resume),complete](const auto& call,std::stop_token stop){return complete(resume(call,stop),stop);};
    return callbacks;
}
}
