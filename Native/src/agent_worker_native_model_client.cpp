#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/agent_model_values.hpp"
#include "agent_worker_sdk_values.hpp"
#include "agentflow/agent_worker_journal_values.hpp"
#include "agentflow/http_stream_transport.hpp"
#include <algorithm>
#include <thread>
#include <mutex>
#include <optional>
#include <map>
namespace agentflow {
namespace {

}
struct NativeWorkerModelClient::Impl {
    X::Runtime runtime;
    X::Value root,worker,model,journal;
    std::mutex mutex,journal_mutex;
    std::map<std::string,X::Value> model_methods,journal_methods;
    Impl(const NativeWorkerModelAccess& access,const std::string& root_id,const std::string& owner,const std::string& workspace,const std::string& generation,std::int64_t revision){
        if(!access.endpoint.starts_with("lrpc:")||access.endpoint.size()>32||access.ticket.size()!=64||access.ticket.find_first_not_of("0123456789abcdef")!=std::string::npos||!access.pid)throw Conflict("Invalid native worker model access");
        X3LrpcEndpointInfo info{};info.size=sizeof(info);if(x3_runtime_probe_remote(runtime.get(),access.endpoint.c_str(),1000,&info)!=X3_STATUS_OK||info.pid!=access.pid)throw Conflict("Native model endpoint identity differs");
        root=runtime.ImportRemote("xmind",access.endpoint.c_str());auto services=call(root,"Workers",{});worker=call(services,"Open",{text(access.ticket)});const auto identity=call(worker,"Identity",{});
        if(identity["root"].ToString()!=root_id||identity["owner"].ToString()!=owner||identity["workspace"].ToString()!=workspace||identity["generation"].ToString()!=generation||!identity["revision"].IsInt64()||identity["revision"].ToInt64()!=revision)throw Conflict("Native model object differs from captured worker");model=call(worker,"Model",{});
    }
    X::Value text(const std::string& bytes){return X::Value(runtime,bytes);}
    X::Value number(std::uint64_t n){return X::Value(runtime,n);}
    X::Value invoke(const X::Value& method,std::vector<X::Value> args){X::Value result;if(!method.Call(args,result))throw AgentWorkerModelOutcomeUnrecorded("Native SDK acknowledgement unavailable; no replay authorized");return result;}
    X::Value call(const X::Value& object,const char* method,std::vector<X::Value> args){return invoke(object[method],std::move(args));}
    // Cantor's worker retains remote method handles before its execution loop.
    X::Value service_call(const X::Value& service,std::map<std::string,X::Value>& methods,const char* name,std::vector<X::Value> args){auto found=methods.find(name);if(found==methods.end())found=methods.emplace(name,service[name]).first;return invoke(found->second,std::move(args));}
    X::Value model_call(const char* name,std::vector<X::Value> args){return service_call(model,model_methods,name,std::move(args));}
    X::Value journal_call(const char* name,std::vector<X::Value> args){return service_call(journal,journal_methods,name,std::move(args));}
    std::uint64_t integer(const X::Value& value){if((!value.IsUInt64()&&!value.IsInt64())||(value.IsInt64()&&value.ToInt64()<0))throw AgentWorkerModelOutcomeUnrecorded("Native model count has invalid type");return value.ToUInt64();}
    std::string await(const X::Value& operation,ChatCompletionStream::Sink sink,std::stop_token cancel){
        try{const auto status=operation["Status"],events=operation["Events"];std::uint64_t ordinal=0;X::Value snapshot;for(;;){if(cancel.stop_requested())throw TransportCancelled("Native worker observation cancelled");snapshot=invoke(status,{});if(!snapshot.IsDict()||snapshot.Size()!=5)throw AgentWorkerModelOutcomeUnrecorded("Native operation status differs");const auto done=snapshot["done"];const auto count=integer(snapshot["event_count"]);if(done.raw().tag!=X3_TAG_BOOL||count<ordinal||count>8192)throw AgentWorkerModelOutcomeUnrecorded("Native event order differs");if(count>ordinal){const auto batch=invoke(events,{number(ordinal)});if(!batch.IsList()||batch.Size()<count-ordinal||batch.Size()>8192-ordinal)throw AgentWorkerModelOutcomeUnrecorded("Native event batch differs");for(std::uint64_t i=0;i<batch.Size();++i){auto event=decode_agent_model_event(sdk_values::from_value(batch[i]).dump());if(sink)sink(event);++ordinal;}}if(done.ToInt64()!=0)break;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
            const auto error=snapshot["error"];if(!error.IsString())throw AgentWorkerModelOutcomeUnrecorded("Native model failure has invalid type");const auto code=error.ToString();if(code=="budget_exhausted")throw RootBudgetExhausted("Worker root model allowance exhausted");if(code=="capacity")throw AgentWorkerJournalCapacity("Native worker journal value exceeds capacity");if(code=="rejected")throw Conflict("Native model rejected captured invocation");if(code=="cancelled")throw TransportCancelled("Native model operation cancelled");if(code=="timeout")throw TransportTimeout("Native model operation deadline elapsed");if(code=="provider_http"){const auto status=snapshot["provider_status"],detail=snapshot["provider_code"];if(!status.IsInt64()||status.ToInt64()<100||status.ToInt64()>599||!detail.IsString()||(!detail.ToString().empty()&&(status.ToInt64()!=400||detail.ToString()!="context_length_exceeded")))throw AgentWorkerModelOutcomeUnrecorded("Native provider failure facts differ");ProviderHttpError actual(static_cast<int>(status.ToInt64()));actual.code=detail.ToString();throw actual;}if(code=="model_protocol")throw ModelProtocolError("Native model protocol failed");if(!code.empty())throw AgentWorkerModelOutcomeUnrecorded("Native model outcome is unrecorded; no replay authorized");return sdk_values::from_value(call(operation,"Result",{})).dump();
        }catch(...){try{call(operation,"Cancel",{});}catch(...){}throw;}
    }
    std::string journal_response(const X::Value& value){
        if(!value.IsDict())return await(value,{},{});
        const auto error=value["error"];if(value.Size()!=2||!error.IsString())throw AgentWorkerModelOutcomeUnrecorded("Native journal result differs");const auto code=error.ToString();if(code=="cancelled")throw TransportCancelled("Native journal cancelled");if(code=="timeout")throw TransportTimeout("Native journal deadline elapsed");if(code=="rejected")throw Conflict("Native journal rejected captured invocation");if(code=="budget_exhausted")throw RootBudgetExhausted("Worker root model allowance exhausted");if(code=="capacity")throw AgentWorkerJournalCapacity("Native journal value exceeds capacity");if(!code.empty())throw AgentWorkerModelOutcomeUnrecorded("Native journal outcome is unrecorded; no replay authorized");return sdk_values::from_value(value["value"]).dump();
    }

};
NativeWorkerModelClient::NativeWorkerModelClient(NativeWorkerModelAccess access,std::string root,std::string owner,std::string workspace,std::string generation,std::int64_t revision):impl_(std::make_unique<Impl>(access,root,owner,workspace,generation,revision)){}
NativeWorkerModelClient::~NativeWorkerModelClient()=default;
AgentWorkerModelAdmission NativeWorkerModelClient::admission(std::stop_token cancel){std::lock_guard lock(impl_->mutex);if(cancel.stop_requested())throw TransportCancelled("Native model admission cancelled");return decode_agent_worker_model_admission(impl_->await(impl_->model_call("Admission",{}),{},cancel));}
ModelRequest NativeWorkerModelClient::prepare_request(std::stop_token cancel){std::lock_guard lock(impl_->mutex);if(cancel.stop_requested())throw TransportCancelled("Native model prepare cancelled");return decode_agent_model_request(impl_->await(impl_->model_call("Prepare",{}),{},cancel));}
ModelRequest NativeWorkerModelClient::rebuild_request(const std::string& attempt,const ModelRequest& request,std::stop_token cancel){std::lock_guard lock(impl_->mutex);if(cancel.stop_requested())throw TransportCancelled("Native model rebuild cancelled");auto input=sdk_values::to_value(impl_->runtime.host(),nlohmann::json::parse(encode_agent_model_request(request)));return decode_agent_model_request(impl_->await(impl_->model_call("Rebuild",{impl_->text(attempt),input}),{},cancel));}
ModelCompletion NativeWorkerModelClient::complete(const std::string& attempt,const ModelRequest& request,ChatCompletionStream::Sink sink,std::stop_token cancel){std::lock_guard lock(impl_->mutex);if(cancel.stop_requested())throw TransportCancelled("Native model complete cancelled");if(!sink)throw std::invalid_argument("Native model event sink required");auto input=sdk_values::to_value(impl_->runtime.host(),nlohmann::json::parse(encode_agent_model_request(request)));return decode_agent_model_completion(impl_->await(impl_->model_call("Complete",{impl_->text(attempt),input}),std::move(sink),cancel));}
std::string NativeWorkerModelClient::journal_run(std::string id){std::lock_guard lock(impl_->journal_mutex);if(!impl_->journal.IsValid())impl_->journal=impl_->call(impl_->worker,"Journal",{});return impl_->journal_response(impl_->journal_call("Run",{impl_->text(id)}));}
std::string NativeWorkerModelClient::journal_session(std::string id){std::lock_guard lock(impl_->journal_mutex);if(!impl_->journal.IsValid())impl_->journal=impl_->call(impl_->worker,"Journal",{});return impl_->journal_response(impl_->journal_call("Session",{impl_->text(id)}));}
std::string NativeWorkerModelClient::journal_history(std::string id){std::lock_guard lock(impl_->journal_mutex);if(!impl_->journal.IsValid())impl_->journal=impl_->call(impl_->worker,"Journal",{});return impl_->journal_response(impl_->journal_call("History",{impl_->text(id)}));}
std::string NativeWorkerModelClient::journal_events(std::string id,std::int64_t after){std::lock_guard lock(impl_->journal_mutex);if(!impl_->journal.IsValid())impl_->journal=impl_->call(impl_->worker,"Journal",{});return impl_->journal_response(impl_->journal_call("Events",{impl_->text(id),X::Value(after)}));}
std::string NativeWorkerModelClient::journal_root_budget(std::string id){std::lock_guard lock(impl_->journal_mutex);if(!impl_->journal.IsValid())impl_->journal=impl_->call(impl_->worker,"Journal",{});return impl_->journal_response(impl_->journal_call("RootBudget",{impl_->text(id)}));}
std::string NativeWorkerModelClient::journal_child_admission(std::string id){std::lock_guard lock(impl_->journal_mutex);if(!impl_->journal.IsValid())impl_->journal=impl_->call(impl_->worker,"Journal",{});return impl_->journal_response(impl_->journal_call("ChildAdmission",{impl_->text(id)}));}
std::string NativeWorkerModelClient::journal_workspace_mode(){std::lock_guard lock(impl_->journal_mutex);if(!impl_->journal.IsValid())impl_->journal=impl_->call(impl_->worker,"Journal",{});return impl_->journal_response(impl_->journal_call("WorkspaceMode",{}));}
std::string NativeWorkerModelClient::journal_execution_window(){std::lock_guard lock(impl_->journal_mutex);if(!impl_->journal.IsValid())impl_->journal=impl_->call(impl_->worker,"Journal",{});return impl_->journal_response(impl_->journal_call("ExecutionWindow",{}));}
std::string NativeWorkerModelClient::journal_begin(){std::lock_guard lock(impl_->journal_mutex);if(!impl_->journal.IsValid())impl_->journal=impl_->call(impl_->worker,"Journal",{});return impl_->journal_response(impl_->journal_call("Begin",{}));}
std::string NativeWorkerModelClient::journal_reserve_model(std::string attempt){std::lock_guard lock(impl_->journal_mutex);if(!impl_->journal.IsValid())impl_->journal=impl_->call(impl_->worker,"Journal",{});return impl_->journal_response(impl_->journal_call("ReserveModel",{impl_->text(attempt)}));}
std::string NativeWorkerModelClient::journal_start_model(std::string attempt){std::lock_guard lock(impl_->journal_mutex);if(!impl_->journal.IsValid())impl_->journal=impl_->call(impl_->worker,"Journal",{});return impl_->journal_response(impl_->journal_call("StartModel",{impl_->text(attempt)}));}
std::string NativeWorkerModelClient::journal_finish_model(std::string attempt){std::lock_guard lock(impl_->journal_mutex);if(!impl_->journal.IsValid())impl_->journal=impl_->call(impl_->worker,"Journal",{});return impl_->journal_response(impl_->journal_call("FinishModel",{impl_->text(attempt)}));}
std::string NativeWorkerModelClient::journal_model_event(std::string attempt,std::int64_t ordinal,std::string kind,std::string json){std::lock_guard lock(impl_->journal_mutex);if(!impl_->journal.IsValid())impl_->journal=impl_->call(impl_->worker,"Journal",{});return impl_->journal_response(impl_->journal_call("ModelEvent",{impl_->text(attempt),X::Value(ordinal),impl_->text(kind),impl_->text(json)}));}
std::string NativeWorkerModelClient::journal_tool_event(std::string attempt,std::int64_t ordinal,bool returned){std::lock_guard lock(impl_->journal_mutex);if(!impl_->journal.IsValid())impl_->journal=impl_->call(impl_->worker,"Journal",{});return impl_->journal_response(impl_->journal_call("ToolEvent",{impl_->text(attempt),X::Value(ordinal),X::Value(returned)}));}
std::string NativeWorkerModelClient::journal_workspace_tool_event(std::string attempt,std::int64_t ordinal,bool returned){std::lock_guard lock(impl_->journal_mutex);if(!impl_->journal.IsValid())impl_->journal=impl_->call(impl_->worker,"Journal",{});return impl_->journal_response(impl_->journal_call("WorkspaceToolEvent",{impl_->text(attempt),X::Value(ordinal),X::Value(returned)}));}
std::string NativeWorkerModelClient::journal_commit_read_turn(std::string attempt){std::lock_guard lock(impl_->journal_mutex);if(!impl_->journal.IsValid())impl_->journal=impl_->call(impl_->worker,"Journal",{});return impl_->journal_response(impl_->journal_call("CommitReadTurn",{impl_->text(attempt)}));}
std::string NativeWorkerModelClient::journal_commit_workspace_turn(std::string attempt){std::lock_guard lock(impl_->journal_mutex);if(!impl_->journal.IsValid())impl_->journal=impl_->call(impl_->worker,"Journal",{});return impl_->journal_response(impl_->journal_call("CommitWorkspaceTurn",{impl_->text(attempt)}));}
std::string NativeWorkerModelClient::journal_complete(std::string attempt){std::lock_guard lock(impl_->journal_mutex);if(!impl_->journal.IsValid())impl_->journal=impl_->call(impl_->worker,"Journal",{});return impl_->journal_response(impl_->journal_call("Complete",{impl_->text(attempt)}));}

}
