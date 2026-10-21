#include "agentflow/agent_worker_file_host.hpp"
#include "agentflow/persistence_service.hpp"
#include "agentflow/repository_instruction_context.hpp"
#include "agentflow/edit_executor.hpp"
#include "agentflow/permission_waiter.hpp"
#include "nlohmann/json.hpp"
#include <condition_variable>
#include <thread>

namespace agentflow {
namespace {
AgentFileToolResult failure(const char* code,const char* message){return {false,nlohmann::json{{"error",{{"code",code},{"message",message}}}}.dump()};}
}
AgentWorkerFileHost::AgentWorkerFileHost(PersistenceService& store,AgentWorkerLeaseSpec lease,
    AgentFileToolOwnerContext owner,std::chrono::steady_clock::time_point deadline,std::stop_token cancel)
    :store_(store),lease_(std::move(lease)),deadline_(deadline),owner_cancel_(cancel){
    if(owner.run_id!=lease_.owner_run_id||!owner.primary_workspace||owner.primary_workspace->identity()!=lease_.workspace_id||!owner.repository_context||owner.before_effect)throw std::invalid_argument("Worker file owner differs from captured lease");
    if((owner.workspace_members?owner.workspace_members->revision():1)!=lease_.workspace_revision)throw std::invalid_argument("Worker file workspace revision differs");
    const auto count=owner.workspace_members?owner.workspace_members->folders().size():1;
    for(std::size_t index=0;index<count;++index)if(!owner.repository_context(index))throw std::invalid_argument("Worker file requires native guidance context");
    owner.before_effect=[this](const AgentFileToolInvocation& call,const std::string& id,std::stop_token stop){
        try{store_.claim_agent_worker_file_effect(lease_,call,id,deadline_,owner_cancel_,stop).get();}
        catch(const AgentWorkerAuthorityCancelled&){throw ToolCancelled("Worker file authority cancelled before effect");}
        catch(const AgentWorkerAuthorityExpired&){throw ToolCancelled("Worker file authority expired before effect");}
        catch(const Conflict&){throw ToolAccessDenied("Worker file authority unavailable before effect");}
    };
    native_=make_agent_file_tool_host(store_,std::move(owner));
}
AgentFileToolResult AgentWorkerFileHost::invoke(const AgentFileToolInvocation& call,std::stop_token caller){
    std::unique_lock lock(invocation_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Worker file dispatch is already active");
    if(owner_cancel_.stop_requested()||caller.stop_requested())throw AgentWorkerAuthorityCancelled("Worker file call cancelled before entry");
    if(std::chrono::steady_clock::now()>=deadline_)throw AgentWorkerAuthorityExpired("Worker file call deadline elapsed before entry");
    std::stop_source stop;std::stop_callback owner_stop(owner_cancel_,[&]{stop.request_stop();});std::stop_callback caller_stop(caller,[&]{stop.request_stop();});
    std::mutex timer_mutex;std::condition_variable_any timer_condition;
    std::jthread timer([&](std::stop_token done){std::unique_lock timer_lock(timer_mutex);timer_condition.wait_until(timer_lock,done,deadline_,[]{return false;});if(!done.stop_requested())stop.request_stop();});
    store_.claim_agent_worker_file(lease_,call,deadline_,owner_cancel_,stop.get_token()).get();
    store_.start_agent_worker_file(lease_,call,deadline_,owner_cancel_,stop.get_token()).get();
    const auto save=[&](const AgentFileToolResult& result){try{store_.record_agent_worker_file_result(lease_,call,result.success,result.output_json).get();}catch(...){throw AgentFileToolOutcomeUnrecorded("Worker file result could not be recorded; stop execution and retain native operation ownership");}};
    AgentFileToolResult result;std::exception_ptr interrupted;
    try{result=native_->invoke(call,stop.get_token());}
    catch(const EditOutcomeUnrecorded&){throw AgentFileToolOutcomeUnrecorded("Native file outcome is unrecorded; stop execution and retain operation ownership");}
    catch(const ToolMutationUncertain&){throw AgentFileToolOutcomeUnrecorded("Native file mutation is uncertain; stop execution and retain operation ownership");}
    catch(const AgentPermissionDenied&){result=failure("agent_permission_denied","The selected agent permission rules denied this resource; the tool was not dispatched");}
    catch(const PermissionDenied&){result=failure("permission_denied","Controller denied this operation; the requested effect was not dispatched");}
    catch(const PermissionExpired&){result=failure("permission_expired","Controller approval expired before the requested effect");}
    catch(const PermissionCancelled&){result=failure("worker_file_cancelled","The backend file call was cancelled; inspect native operation receipts before requesting another change");interrupted=std::make_exception_ptr(ToolCancelled("Worker file call cancelled"));}
    catch(const ToolCancelled&){result=failure("worker_file_cancelled","The backend file call was cancelled; inspect native operation receipts before requesting another change");interrupted=std::current_exception();}
    catch(const ToolGuidanceChanged&){result=failure("repository_instructions_required","Updated repository guidance must reach the next model request before another change");}
    catch(const ToolContentConflict&){result=failure("file_content_conflict","The native file precondition changed; inspect current content before requesting another change");}
    catch(const ToolEditNeedsPatch&){result=failure("edit_requires_patch","This edit requires apply_patch rather than exact text replacement");}
    catch(const ToolAccessDenied&){result=failure("file_access_denied","Native file access was denied; inspect operation receipts before requesting another change");}
    catch(const ToolFileError&){result=failure("native_file_error","The native file call could not access its requested file or directory");}
    catch(const std::invalid_argument&){result=failure("invalid_tool_arguments","The native file call rejected its argument schema");}
    catch(...){const auto error=std::current_exception();save(failure("worker_file_failed","The backend file call did not return a result; inspect native operation receipts before requesting another change"));std::rethrow_exception(error);}
    save(result);if(interrupted)std::rethrow_exception(interrupted);
    // Late actual outcomes remain facts, but revocation/cancellation cannot
    // turn them into permission to continue or deliver to a stale worker.
    store_.read_agent_worker_journal(lease_,{AgentWorkerJournalReadKind::run,lease_.owner_run_id}).get();
    if(stop.stop_requested()||std::chrono::steady_clock::now()>=deadline_)throw ToolCancelled("Worker file owner cancelled or expired before delivery");
    return result;
}
}
