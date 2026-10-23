#include "agent_worker_rpc_fixture.hpp"
#include "../src/agent_worker_journal_rpc_values.hpp"
namespace agentflow {
static FixtureWorkerRpcHandler bind_journal_rpc(PersistenceService& store,AgentWorkerLeaseSpec lease,const AgentWorkerScope& scope,AgentWorkerChannel::Deadline deadline,std::stop_token owner_cancel,AgentWorkerFileHost* files,AgentWorkerJournalModelHooks hooks){
    if(scope!=AgentWorkerScope{lease.root_run_id,lease.owner_run_id,lease.workspace_id,lease.generation,lease.workspace_revision})throw Conflict("Journal channel differs from captured lease");
    const bool workspace=files!=nullptr;
    auto host=files?std::make_shared<AgentWorkerJournalHost>(store,*files,deadline,owner_cancel,std::move(hooks)):std::make_shared<AgentWorkerJournalHost>(store,std::move(lease),deadline,owner_cancel,std::move(hooks));
    return [host=std::move(host),deadline,owner_cancel,workspace](const std::string& method,const std::string& payload,AgentWorkerRpcSink,std::stop_token cancel)->std::string{
        try{if(cancel.stop_requested()||owner_cancel.stop_requested())throw AgentWorkerRpcRemoteError("cancelled");if(std::chrono::steady_clock::now()>=deadline)throw AgentWorkerRpcRemoteError("timeout");const auto v=parse(payload);
            if(method=="journal.workspace_mode"){object(v,0);return Json{{"coding",host->coding_workspace_admitted(cancel)}}.dump();}
            if(method=="journal.execution_window"){object(v,0);return Json{{"remaining_ms",host->remaining_ms(cancel)}}.dump();}
            if(method=="journal.begin_run"){object(v,0);return encode_worker_journal_value(host->begin_run(cancel));}
            if(method=="journal.model_event"){object(v,4);return encode_worker_journal_value(std::vector<Event>{host->model_event(attempt_id(v.at("attempt_id")),integer(v.at("ordinal")),text(v.at("kind")),text(v.at("json")),cancel)});}
            if(method=="journal.tool_event"||(workspace&&method=="journal.workspace_tool_event")){object(v,3);const auto phase=text(v.at("phase"));if(phase!="started"&&phase!="returned")invalid();const auto id=attempt_id(v.at("attempt_id"));const auto ordinal=integer(v.at("ordinal"));return encode_worker_journal_value(std::vector<Event>{method=="journal.tool_event"?host->tool_event(id,ordinal,phase=="returned",cancel):host->workspace_tool_event(id,ordinal,phase=="returned",cancel)});}
            if(method=="journal.commit_read_turn"||(workspace&&method=="journal.commit_workspace_turn")){object(v,1);const auto id=attempt_id(v.at("attempt_id"));return encode_worker_tool_turn(method=="journal.commit_read_turn"?host->commit_read_turn(id,cancel):host->commit_workspace_turn(id,cancel));}
            if(method=="journal.reserve_model"||method=="journal.start_model"||method=="journal.finish_model"||method=="journal.complete_run"){object(v,1);const auto id=attempt_id(v.at("attempt_id"));if(method=="journal.complete_run")return encode_worker_journal_value(host->complete_run(id,cancel));return encode_worker_model_reservation(method=="journal.reserve_model"?host->reserve_model(id,cancel):method=="journal.start_model"?host->start_model(id,cancel):host->finish_model(id,cancel));}
            const auto k=kind(method);object(v,2);const auto target=text(v.at("target"));const auto after=integer(v.at("after"));if(k!=AgentWorkerJournalReadKind::events&&after!=0)invalid();
            switch(k){case AgentWorkerJournalReadKind::run:return encode_worker_journal_value(host->run(target,cancel));case AgentWorkerJournalReadKind::session:return encode_worker_journal_value(host->session(target,cancel));case AgentWorkerJournalReadKind::history:return encode_worker_journal_value(host->history(target,cancel));case AgentWorkerJournalReadKind::events:return encode_worker_journal_value(host->events(target,after,cancel));case AgentWorkerJournalReadKind::child_admission:return encode_worker_journal_value(host->child_admission(target,cancel));case AgentWorkerJournalReadKind::root_budget:return encode_worker_journal_value(host->root_budget(target,cancel));}invalid();}
        catch(const AgentWorkerRpcRemoteError&){throw;}
        catch(const AgentWorkerJournalCapacity&){throw AgentWorkerRpcRemoteError("capacity");}
        catch(const AgentWorkerAuthorityCancelled&){throw AgentWorkerRpcRemoteError("cancelled");}catch(const AgentWorkerAuthorityExpired&){throw AgentWorkerRpcRemoteError("timeout");}
        catch(const RootBudgetExhausted&){throw AgentWorkerRpcRemoteError("budget_exhausted");}
        catch(const Conflict&){throw AgentWorkerRpcRemoteError("rejected");}catch(const NotFound&){throw AgentWorkerRpcRemoteError("rejected");}
        catch(const AgentWorkerProtocolError&){throw AgentWorkerRpcRemoteError("rejected");}catch(const std::invalid_argument&){throw AgentWorkerRpcRemoteError("rejected");}catch(const Json::exception&){throw AgentWorkerRpcRemoteError("rejected");}
    };
}
FixtureWorkerRpcHandler bind_agent_worker_journal_rpc(PersistenceService& store,AgentWorkerLeaseSpec lease,const AgentWorkerScope& scope,AgentWorkerChannel::Deadline deadline,std::stop_token cancel,AgentWorkerJournalModelHooks hooks){return bind_journal_rpc(store,std::move(lease),scope,deadline,cancel,nullptr,std::move(hooks));}
FixtureWorkerRpcHandler bind_agent_worker_workspace_journal_rpc(PersistenceService& store,AgentWorkerFileHost& files,const AgentWorkerScope& scope,AgentWorkerChannel::Deadline deadline,std::stop_token cancel,AgentWorkerJournalModelHooks hooks){return bind_journal_rpc(store,files.lease(),scope,deadline,cancel,&files,std::move(hooks));}

}
