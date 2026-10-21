#include "agentflow/agent_worker_process_host.hpp"
#include "agentflow/process_executor.hpp"
#include "agentflow/repository_instruction_context.hpp"
#include "nlohmann/json.hpp"
#include <condition_variable>
namespace agentflow {
namespace {AgentProcessToolResult failure(const char* code){return {false,nlohmann::json{{"error",{{"code",code},{"message","Backend process did not return an acknowledged command result; inspect native operation receipts before requesting another command"}}}}.dump()};}}
AgentWorkerProcessHost::AgentWorkerProcessHost(PersistenceService& store,AgentWorkerLeaseSpec lease,AgentProcessToolOwnerContext owner,std::chrono::steady_clock::time_point deadline,std::stop_token cancel)
    :store_(store),lease_(std::move(lease)),deadline_(deadline),owner_cancel_(cancel){
    if(owner.run_id!=lease_.owner_run_id||!owner.primary_workspace||owner.primary_workspace->identity()!=lease_.workspace_id||!owner.repository_context||!owner.offered_tool||owner.before_effect||owner.allow_background||owner.deadline!=deadline_||(owner.workspace_members?owner.workspace_members->revision():1)!=lease_.workspace_revision)throw std::invalid_argument("Worker process owner differs from captured foreground lease");
    const auto count=owner.workspace_members?owner.workspace_members->folders().size():1;for(std::size_t i=0;i<count;++i)if(!owner.repository_context(i))throw std::invalid_argument("Worker process requires native guidance");
    auto offered=owner.offered_tool;owner.offered_tool=[offered=std::move(offered)](const std::string& name){return is_agent_process_tool(name)&&offered(name);};
    owner.before_effect=[this](const AgentProcessToolInvocation& call,const std::string& id,std::stop_token token){store_.claim_agent_worker_process_effect(lease_,call,id,deadline_,owner_cancel_,token).get();};
    native_=make_agent_process_tool_host(store_,std::move(owner));
}
AgentProcessToolResult AgentWorkerProcessHost::invoke(const AgentProcessToolInvocation& call,std::stop_token caller){
    std::unique_lock lock(invocation_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Worker process dispatch is already active");
    if(owner_cancel_.stop_requested()||caller.stop_requested())throw AgentWorkerAuthorityCancelled("Worker process cancelled before entry");
    if(std::chrono::steady_clock::now()>=deadline_)throw AgentWorkerAuthorityExpired("Worker process deadline elapsed before entry");
    std::stop_source stop;std::stop_callback owner_stop(owner_cancel_,[&]{stop.request_stop();});std::stop_callback caller_stop(caller,[&]{stop.request_stop();});
    std::mutex timer_mutex;std::condition_variable_any condition;std::jthread timer([&](std::stop_token done){std::unique_lock timer_lock(timer_mutex);condition.wait_until(timer_lock,done,deadline_,[]{return false;});if(!done.stop_requested())stop.request_stop();});
    store_.claim_agent_worker_process(lease_,call,deadline_,owner_cancel_,stop.get_token()).get();store_.start_agent_worker_process(lease_,call,deadline_,owner_cancel_,stop.get_token()).get();
    const auto save=[&](const AgentProcessToolResult& result){try{store_.record_agent_worker_process_result(lease_,call,result.success,result.output_json).get();}catch(...){throw ProcessOutcomeUnrecorded("Worker process result could not be recorded; retain native ownership without replay");}};
    AgentProcessToolResult result;std::exception_ptr interrupted;
    try{result=native_->invoke(call,stop.get_token());}
    catch(const ProcessOutcomeUnrecorded&){throw;}
    catch(const ProcessEffectUncertain&){throw;}
    catch(const ProcessBeforeDispatchError&){result=failure("process_not_dispatched");}
    catch(const AgentPermissionDenied&){result=failure("agent_permission_denied");}
    catch(const PermissionDenied&){result=failure("permission_denied");}
    catch(const PermissionExpired&){result=failure("permission_expired");}
    catch(const PermissionCancelled&){result=failure("process_cancelled");interrupted=std::make_exception_ptr(ToolCancelled("Worker process cancelled"));}
    catch(const ToolCancelled&){result=failure("process_cancelled");interrupted=std::current_exception();}
    catch(const ToolGuidanceChanged&){result=failure("repository_instructions_required");}
    catch(const ToolAccessDenied&){result=failure("process_access_denied");}
    catch(const std::invalid_argument&){result=failure("invalid_tool_arguments");}
    catch(...){save(failure("worker_process_failed"));throw;}
    save(result);if(interrupted)std::rethrow_exception(interrupted);
    store_.read_agent_worker_journal(lease_,{AgentWorkerJournalReadKind::run,lease_.owner_run_id}).get();if(stop.stop_requested()||std::chrono::steady_clock::now()>=deadline_)throw ToolCancelled("Worker process cancelled before result delivery");return result;
}
}
