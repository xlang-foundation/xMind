#include "agentflow/agent_worker_remote_host.hpp"
#include "agentflow/persistence_service.hpp"
#include "agentflow/mcp_tool_registry.hpp"
#include "agentflow/a2a_tool_registry.hpp"
#include "agentflow/repository_instruction_context.hpp"
#include "nlohmann/json.hpp"
#include <condition_variable>
namespace agentflow {
namespace {AgentRemoteToolResult failure(const char* code){return {false,nlohmann::json{{"error",{{"code",code},{"message","Backend remote tool did not return an acknowledged result; inspect native operation receipts before requesting another command"}}}}.dump()};}}
AgentWorkerRemoteHost::AgentWorkerRemoteHost(PersistenceService& store,AgentWorkerLeaseSpec lease,AgentRemoteToolOwnerContext owner,std::chrono::steady_clock::time_point deadline,std::stop_token cancel)
    :store_(store),lease_(std::move(lease)),deadline_(deadline),owner_cancel_(cancel){
    if(owner.run_id!=lease_.owner_run_id||!owner.primary_workspace||owner.primary_workspace->identity()!=lease_.workspace_id||(!owner.repository_context&&!owner.repository_context_for_call)||!owner.offered_tool||owner.before_effect||owner.deadline!=deadline_||(owner.workspace_members?owner.workspace_members->revision():1)!=lease_.workspace_revision)throw std::invalid_argument("Worker remote owner differs from captured lease");
    auto offered=owner.offered_tool;owner.offered_tool=[offered=std::move(offered)](const std::string& name){return is_agent_remote_tool(name)&&offered(name);};
    owner.before_effect=[store=&store_,lease=lease_,deadline=deadline_,cancel=owner_cancel_](const AgentRemoteToolInvocation& call,const std::string& id,std::stop_token token){store->claim_agent_worker_remote_effect(lease,call,id,deadline,cancel,token).get();};
    native_=make_agent_remote_tool_host(store_,store_,std::move(owner));
}
AgentRemoteToolResult AgentWorkerRemoteHost::invoke(const AgentRemoteToolInvocation& call,std::stop_token caller){
    std::unique_lock lock(invocation_,std::try_to_lock);if(!lock.owns_lock())throw Conflict("Worker remote dispatch is already active");
    if(owner_cancel_.stop_requested()||caller.stop_requested())throw AgentWorkerAuthorityCancelled("Worker remote cancelled before entry");
    if(std::chrono::steady_clock::now()>=deadline_)throw AgentWorkerAuthorityExpired("Worker remote deadline elapsed before entry");
    std::stop_source stop;std::stop_callback owner_stop(owner_cancel_,[&]{stop.request_stop();});std::stop_callback caller_stop(caller,[&]{stop.request_stop();});
    std::mutex timer_mutex;std::condition_variable_any condition;std::jthread timer([&](std::stop_token done){std::unique_lock timer_lock(timer_mutex);condition.wait_until(timer_lock,done,deadline_,[]{return false;});if(!done.stop_requested())stop.request_stop();});
    store_.claim_agent_worker_remote(lease_,call,deadline_,owner_cancel_,stop.get_token()).get();store_.start_agent_worker_remote(lease_,call,deadline_,owner_cancel_,stop.get_token()).get();
    const auto save=[&](const AgentRemoteToolResult& result){try{store_.record_agent_worker_remote_result(lease_,call,result.success,result.output_json).get();}catch(...){throw AgentWorkerRemoteOutcomeUnrecorded("Worker remote result could not be recorded; retain native ownership without replay");}};
    AgentRemoteToolResult result;std::exception_ptr interrupted;
    try{result=native_->invoke(call,stop.get_token());}
    catch(const AgentWorkerRemoteOutcomeUnrecorded&){throw;}
    catch(const McpOutcomeUnrecorded&){throw;}
    catch(const A2aOutcomeUnrecorded&){throw;}
    catch(const McpEffectUncertain&){throw;}
    catch(const A2aEffectUncertain&){throw;}
    catch(const McpEffectNotDispatched&){result=failure("remote_not_dispatched");}
    catch(const A2aEffectNotDispatched&){result=failure("remote_not_dispatched");}
    catch(const McpReadFailed&){result=failure("remote_read_failed");}
    catch(const McpCatalogueChanged&){result=failure("remote_catalogue_changed");}
    catch(const TransportCancelled&){result=failure("remote_cancelled");interrupted=std::make_exception_ptr(ToolCancelled("Worker remote cancelled"));}
    catch(const TransportTimeout&){result=failure("remote_timeout");interrupted=std::make_exception_ptr(ToolCancelled("Worker remote deadline elapsed"));}
    catch(const AgentPermissionDenied&){result=failure("agent_permission_denied");}
    catch(const PermissionDenied&){result=failure("permission_denied");}
    catch(const PermissionExpired&){result=failure("permission_expired");}
    catch(const PermissionCancelled&){result=failure("remote_cancelled");interrupted=std::make_exception_ptr(ToolCancelled("Worker remote cancelled"));}
    catch(const ToolCancelled&){result=failure("remote_cancelled");interrupted=std::current_exception();}
    catch(const ToolGuidanceChanged&){result=failure("repository_instructions_required");}
    catch(const ToolAccessDenied&){result=failure("remote_access_denied");}
    catch(const std::invalid_argument&){result=failure("invalid_tool_arguments");}
    catch(...){save(failure("worker_remote_failed"));throw;}
    save(result);if(interrupted)std::rethrow_exception(interrupted);
    store_.read_agent_worker_journal(lease_,{AgentWorkerJournalReadKind::run,lease_.owner_run_id}).get();if(stop.stop_requested()||std::chrono::steady_clock::now()>=deadline_)throw ToolCancelled("Worker remote cancelled before result delivery");return result;
}
}
