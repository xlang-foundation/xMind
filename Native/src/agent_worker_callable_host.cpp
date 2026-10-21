#include "agentflow/agent_worker_callable_host.hpp"
#include "agentflow/persistence_service.hpp"
#include "agentflow/permission_waiter.hpp"
#include "agentflow/repository_instruction_context.hpp"
#include "nlohmann/json.hpp"
#include <condition_variable>
namespace agentflow {
namespace {AgentCallableToolResult failure(const char* code){return {false,nlohmann::json{{"error",{{"code",code},{"message","Backend callable tool did not return an acknowledged result; inspect native operation receipts before requesting another command"}}}}.dump()};}}
AgentWorkerCallableHost::AgentWorkerCallableHost(PersistenceService& store,AgentWorkerLeaseSpec lease,AgentCallableToolOwnerContext owner,std::chrono::steady_clock::time_point deadline,std::stop_token cancel)
    :store_(store),lease_(std::move(lease)),deadline_(deadline),owner_cancel_(cancel){
    if(owner.run_id!=lease_.owner_run_id||owner.workspace_id!=lease_.workspace_id||owner.workspace_revision!=lease_.workspace_revision||(!owner.repository_context&&!owner.repository_context_for_call)||!owner.offered_tool||owner.before_effect||owner.deadline!=deadline_)throw std::invalid_argument("Worker callable owner differs from captured lease");
    owner.before_effect=[store=&store_,lease=lease_,deadline=deadline_,cancel=owner_cancel_](const AgentCallableInvocation& call,const std::string& id,std::stop_token token){store->claim_agent_worker_callable_effect(lease,call,id,deadline,cancel,token).get();};
    native_=make_agent_callable_tool_host(store_,std::move(owner));
}
AgentCallableToolResult AgentWorkerCallableHost::invoke(const AgentCallableInvocation& call,std::stop_token caller){
    std::unique_lock lock(invocation_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Worker callable dispatch is already active");
    if(owner_cancel_.stop_requested()||caller.stop_requested())throw AgentWorkerAuthorityCancelled("Worker callable cancelled before entry");
    if(std::chrono::steady_clock::now()>=deadline_)throw AgentWorkerAuthorityExpired("Worker callable deadline elapsed before entry");
    std::stop_source stop;std::stop_callback owner_stop(owner_cancel_,[&]{stop.request_stop();});std::stop_callback caller_stop(caller,[&]{stop.request_stop();});
    std::mutex timer_mutex;std::condition_variable_any condition;std::jthread timer([&](std::stop_token done){std::unique_lock timer_lock(timer_mutex);condition.wait_until(timer_lock,done,deadline_,[]{return false;});if(!done.stop_requested())stop.request_stop();});
    store_.claim_agent_worker_callable(lease_,call,deadline_,owner_cancel_,stop.get_token()).get();store_.start_agent_worker_callable(lease_,call,deadline_,owner_cancel_,stop.get_token()).get();
    const auto save=[&](const AgentCallableToolResult& result){try{store_.record_agent_worker_callable_result(lease_,call,result.success,result.output_json).get();}catch(...){throw AgentWorkerCallableOutcomeUnrecorded("Worker callable result could not be recorded; retain native ownership without replay");}};
    AgentCallableToolResult result;std::exception_ptr interrupted;
    try{result=native_->invoke(call,stop.get_token());}
    catch(const AgentWorkerCallableOutcomeUnrecorded&){throw;}
    catch(const AgentCallableOutcomeUnrecorded&){throw;}
    catch(const AgentCallableEffectUncertain&){throw;}
    catch(const AgentPermissionDenied&){result=failure("agent_permission_denied");}
    catch(const PermissionDenied&){result=failure("permission_denied");}
    catch(const PermissionExpired&){result=failure("permission_expired");}
    catch(const PermissionCancelled&){result=failure("callable_cancelled");interrupted=std::make_exception_ptr(ToolCancelled("Worker callable cancelled"));}
    catch(const ToolCancelled&){result=failure("callable_cancelled");interrupted=std::current_exception();}
    catch(const ToolGuidanceChanged&){result=failure("repository_instructions_required");}
    catch(const ToolAccessDenied&){result=failure("callable_access_denied");}
    catch(const std::invalid_argument&){result=failure("invalid_tool_arguments");}
    catch(...){save(failure("worker_callable_failed"));throw;}
    save(result);if(interrupted)std::rethrow_exception(interrupted);
    store_.read_agent_worker_journal(lease_,{AgentWorkerJournalReadKind::run,lease_.owner_run_id}).get();if(stop.stop_requested()||std::chrono::steady_clock::now()>=deadline_)throw ToolCancelled("Worker callable cancelled before result delivery");return result;
}
}
