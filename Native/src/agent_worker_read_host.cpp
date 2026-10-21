#include "agentflow/agent_worker_read_host.hpp"
#include "agentflow/persistence_service.hpp"
#include "agentflow/repository_instruction_context.hpp"
#include "agentflow/permission_waiter.hpp"
#include "agentflow/agent_permissions.hpp"
#include <condition_variable>
#include <thread>

namespace agentflow {
AgentWorkerReadHost::AgentWorkerReadHost(AgentRunJournal& journal,PersistenceService& store,AgentWorkerLeaseSpec lease,AgentReadToolOwnerContext owner,std::stop_token cancel)
    :store_(store),lease_(std::move(lease)),owner_(std::move(owner)),owner_cancel_(cancel){
    if(owner_.run_id!=lease_.owner_run_id||!owner_.primary_workspace||owner_.primary_workspace->identity()!=lease_.workspace_id||!owner_.repository_context||owner_.authorize_dispatch)throw std::invalid_argument("Worker read owner differs from captured lease");
    // Unlike the compatibility local owner, worker admission requires actual
    // native guidance contexts for every captured folder; no empty fallback.
    const auto count=owner_.workspace_members?owner_.workspace_members->folders().size():1;
    if((owner_.workspace_members?owner_.workspace_members->revision():1)!=lease_.workspace_revision)throw std::invalid_argument("Worker read workspace revision differs");
    for(std::size_t index=0;index<count;++index)if(!owner_.repository_context(index))throw std::invalid_argument("Worker read requires native guidance context");
    auto native_owner=owner_;native_owner.authorize_dispatch=[this](const AgentWorkspaceInvocation& call){store_.start_agent_worker_read(lease_,call,owner_.deadline,owner_cancel_,active_cancel_).get();};
    AgentGuidanceToolOwnerContext guidance;guidance.run_id=owner_.run_id;guidance.primary_workspace=owner_.primary_workspace;guidance.workspace_members=owner_.workspace_members;guidance.repository_context=owner_.repository_context;guidance.offered_tool=owner_.offered_tool;guidance.inherited_permission_layers=owner_.inherited_permission_layers;guidance.permission_rules=owner_.permission_rules;guidance.deadline=owner_.deadline;guidance.base_instruction_bytes=owner_.guidance_instruction_bytes;guidance.authorize_dispatch=native_owner.authorize_dispatch;guidance_=make_agent_guidance_tool_host(journal,std::move(guidance));
    native_=make_agent_read_tool_host(journal,store_,std::move(native_owner));
}
AgentReadToolResult AgentWorkerReadHost::invoke(const AgentWorkspaceInvocation& call,std::stop_token caller){
    std::unique_lock lock(invocation_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Worker read dispatch is already active");
    std::stop_source stop;std::stop_callback owner_stop(owner_cancel_,[&]{stop.request_stop();});std::stop_callback caller_stop(caller,[&]{stop.request_stop();});
    std::mutex timer_mutex;std::condition_variable_any timer_condition;
    std::jthread timer([&](std::stop_token done){std::unique_lock timer_lock(timer_mutex);timer_condition.wait_until(timer_lock,done,owner_.deadline,[]{return false;});if(!done.stop_requested())stop.request_stop();});
    store_.claim_agent_worker_read(lease_,call,owner_.deadline,owner_cancel_,stop.get_token()).get();active_cancel_=stop.get_token();
    const auto save=[&](const AgentReadToolResult& result){try{store_.record_agent_worker_read_result(lease_,call,result.success,result.output_json).get();}catch(...){throw AgentWorkerReadOutcomeUnrecorded("Worker read result could not be recorded; stop execution and recover ownership");}};
    AgentReadToolResult result;
    try{if(is_agent_guidance_tool(call.tool_name)){const auto actual=guidance_->invoke(call,stop.get_token());result={actual.success,actual.output_json};}else result=native_->invoke(call,stop.get_token());}
    catch(const AgentPermissionDenied&){result={false,R"({"error":{"code":"agent_permission_denied","message":"The selected agent permission rules denied this resource; the tool was not dispatched"}})"};}
    catch(const PermissionDenied&){result={false,R"({"error":{"code":"permission_denied","message":"Controller denied this operation; the requested effect was not dispatched"}})"};}
    catch(...){const auto original=std::current_exception();save({false,R"({"error":{"code":"worker_read_failed","message":"The backend read did not return a result."}})"});std::rethrow_exception(original);}
    save(result);
    // Retain an actual late result as fact, but do not hand its bytes to a
    // caller whose captured owner has since been cancelled or revoked.
    store_.read_agent_worker_journal(lease_,{AgentWorkerJournalReadKind::run,lease_.owner_run_id}).get();
    if(stop.stop_requested()||std::chrono::steady_clock::now()>=owner_.deadline)throw ToolCancelled("Worker read owner cancelled or expired before delivery");
    return result;
}
}
