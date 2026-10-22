#include "agentflow/agent_worker_planning_owner.hpp"
#include "agentflow/agent_runner.hpp"
#include "agentflow/agent_authority.hpp"
#include "agentflow/root_execution_budget.hpp"
#include "nlohmann/json.hpp"

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

    void check(std::stop_token stop={})const{
        if(cancel.stop_requested()||stop.stop_requested())throw TransportCancelled("Planning owner cancelled");
        if(std::chrono::steady_clock::now()>=deadline)throw TransportTimeout("Planning owner expired");
        if(!execution||!budget||!settings.planning||!settings.workspace||lease.root_run_id!=lease.owner_run_id||budget->root_id()!=lease.root_run_id||budget->deadline()!=deadline||!budget->segment_open())throw DynamicPlanUnavailable("Planning requires its captured ordinary root owner");
        budget->check(stop);
        (void)store.read_agent_worker_journal(lease,{AgentWorkerJournalReadKind::run,lease.owner_run_id}).get();
        const auto segment=store.dynamic_budget_segment(lease.root_run_id).get();
        if(segment.state!="open"||segment.id!=budget->segment_id()||!capabilities.catalogue_finalized||capabilities.workspace_identity!=lease.workspace_id||Json::parse(capabilities.provider_identity_json)!=Json::parse(lease.provider_identity_json)||agent_authority_identity(settings,settings.provider.model,capabilities.workspace_identity,agent_authority_credentials(store,settings))!=capabilities.backend_identity)throw DynamicPlanUnavailable("Planning owner authority changed");
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
            return execution->invoke(lease.owner_run_id,{claim.call_id,claim.tool_name,claim.arguments_json},model.actual_assistant_json,attempt,settings,capabilities,budget,linked.get_token(),guidance);
        }catch(const DynamicPlanRejected& error){
            try{store.record_rejected_dynamic_tool_turn(lease.owner_run_id,attempt,model.actual_assistant_json,error.code).get();}
            catch(...){throw DynamicOutcomeUnrecorded("Planning rejection was not durably committed");}
            throw;
        }
    }
    DynamicExecutionResult resume(const std::string& call,std::stop_token stop){
        std::stop_source linked;std::stop_callback owner_stop(cancel,[&]{linked.request_stop();});std::stop_callback caller_stop(stop,[&]{linked.request_stop();});check(linked.get_token());
        const auto accepted=store.dynamic_plan_call(call).get();
        const auto claim=store.agent_worker_planning_claim(lease.root_run_id,accepted.origin_attempt_id).get();
        if(accepted.root_run_id!=lease.root_run_id||accepted.owner_run_id!=lease.owner_run_id||claim.model.generation!=lease.generation||claim.state!="accepted"||claim.native_call_id!=call||claim.segment_id!=budget->segment_id())throw DynamicPlanUnavailable("Planning resume differs from its captured accepted call");
        return execution->resume(lease.owner_run_id,call,settings,capabilities,budget,linked.get_token(),guidance);
    }
};
}
NativePlanningCallbacks make_agent_worker_planning_callbacks(PersistenceService& store,AgentWorkerLeaseSpec lease,AgentSettings settings,
    std::shared_ptr<DynamicPlanExecution> execution,std::shared_ptr<RootExecutionBudget> budget,
    AgentWorkerChannel::Deadline deadline,std::stop_token cancel,DynamicInstructionPrecondition guidance){
    auto owner=std::make_shared<Owner>(Owner{store,std::move(lease),std::move(settings),{},std::move(execution),std::move(budget),deadline,cancel,std::move(guidance)});
    owner->capabilities=store.dynamic_capabilities(owner->lease.root_run_id).get();owner->check();
    NativePlanningCallbacks result;const auto& captured=owner->lease;result.scope={captured.root_run_id,captured.owner_run_id,captured.workspace_id,captured.generation,captured.workspace_revision};
    result.invoke=[owner](const auto& attempt,const auto& call,std::stop_token stop){return owner->invoke(attempt,call,stop);};
    result.resume=[owner](const auto& call,std::stop_token stop){return owner->resume(call,stop);};
    result.healthy=[owner]{try{owner->check();return true;}catch(...){return false;}};
    return result;
}
}
