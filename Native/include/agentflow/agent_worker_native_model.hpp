#pragma once
#include "agentflow/agent_worker_services.hpp"
#include "agentflow/agent_worker_model_host.hpp"
#include "agentflow/agent_read_tool_host.hpp"
#include "agentflow/agent_file_tool_host.hpp"
#include "agentflow/agent_worker_process_service.hpp"
#include "agentflow/agent_remote_tool_host.hpp"
#include "agentflow/agent_worker_callable_service.hpp"
#include "xlang3/xlang3.h"
#include <memory>
#include <chrono>
namespace agentflow {
class AgentWorkerJournalHost;
class NativePlanningBinding;
class DynamicPlanExecution;
struct NativeWorkerModelState;
struct NativeWorkerModelAccess {std::string endpoint,ticket;std::uint32_t pid=0;};
class NativeWorkerModelBinding final {
public:
    // Captured service hosts, including planning, outlive this binding and callers.
    explicit NativeWorkerModelBinding(AgentWorkerModelHost&,AgentWorkerJournalHost* journal=nullptr,AgentReadToolHost* reads=nullptr,AgentFileToolHost* files=nullptr,AgentWorkerProcessService* processes=nullptr,AgentRemoteToolHost* remotes=nullptr,AgentWorkerCallableService* callables=nullptr,NativePlanningBinding* planning=nullptr);
    ~NativeWorkerModelBinding();
    NativeWorkerModelBinding(const NativeWorkerModelBinding&)=delete;
    NativeWorkerModelBinding& operator=(const NativeWorkerModelBinding&)=delete;
    NativeWorkerModelAccess access()const;
    bool outcome_unrecorded()const noexcept;
private:std::shared_ptr<NativeWorkerModelState> state_;std::string ticket_;
};
struct NativeWorkerModelApi;
struct NativeWorkerApi;
struct NativeWorkersApi;
struct NativeWorkerModelApi {
    std::shared_ptr<NativeWorkerModelState> state;
    NativeWorkerModelApi()=default;
    explicit NativeWorkerModelApi(NativeWorkerApi* worker);
    X::Value Admission();
    X::Value Prepare();
    X::Value Rebuild(std::string attempt,X::Value request);
    X::Value Complete(std::string attempt,X::Value request);
    BEGIN_PACKAGE(NativeWorkerModelApi)
        APISET().AddFunc<0>("Admission",&NativeWorkerModelApi::Admission);
        APISET().AddFunc<0>("Prepare",&NativeWorkerModelApi::Prepare);
        APISET().AddFunc<2>("Rebuild",&NativeWorkerModelApi::Rebuild,X3_NATIVE_IPC_ARGS_BY_VALUE);
        APISET().AddFunc<2>("Complete",&NativeWorkerModelApi::Complete,X3_NATIVE_IPC_ARGS_BY_VALUE);
    END_PACKAGE
};
struct NativeWorkerJournalApi {
    std::shared_ptr<NativeWorkerModelState> state;
    NativeWorkerJournalApi()=default;
    explicit NativeWorkerJournalApi(NativeWorkerApi*);
    X::Value Run(std::string id);
    X::Value Session(std::string id);
    X::Value History(std::string id);
    X::Value Events(std::string id,std::int64_t after);
    X::Value RootBudget(std::string id);
    X::Value ChildAdmission(std::string id);
    X::Value WorkspaceMode();
    X::Value ExecutionWindow();
    X::Value Begin();
    X::Value ReserveModel(std::string attempt);
    X::Value ReservePlanningContinuation(std::string call,std::string attempt);
    X::Value StartModel(std::string attempt);
    X::Value FinishModel(std::string attempt);
    X::Value ModelEvent(std::string attempt,std::int64_t ordinal,std::string kind,std::string json);
    X::Value ToolEvent(std::string attempt,std::int64_t ordinal,bool returned);
    X::Value WorkspaceToolEvent(std::string attempt,std::int64_t ordinal,bool returned);
    X::Value CommitReadTurn(std::string attempt);
    X::Value CommitWorkspaceTurn(std::string attempt);
    X::Value Complete(std::string attempt);
    BEGIN_PACKAGE(NativeWorkerJournalApi)
        APISET().AddFunc<1>("Run",&NativeWorkerJournalApi::Run);
        APISET().AddFunc<1>("Session",&NativeWorkerJournalApi::Session);
        APISET().AddFunc<1>("History",&NativeWorkerJournalApi::History);
        APISET().AddFunc<2>("Events",&NativeWorkerJournalApi::Events);
        APISET().AddFunc<1>("RootBudget",&NativeWorkerJournalApi::RootBudget);
        APISET().AddFunc<1>("ChildAdmission",&NativeWorkerJournalApi::ChildAdmission);
        APISET().AddFunc<0>("WorkspaceMode",&NativeWorkerJournalApi::WorkspaceMode);
        APISET().AddFunc<0>("ExecutionWindow",&NativeWorkerJournalApi::ExecutionWindow);
        APISET().AddFunc<0>("Begin",&NativeWorkerJournalApi::Begin);
        APISET().AddFunc<1>("ReserveModel",&NativeWorkerJournalApi::ReserveModel);
        APISET().AddFunc<2>("ReservePlanningContinuation",&NativeWorkerJournalApi::ReservePlanningContinuation);
        APISET().AddFunc<1>("StartModel",&NativeWorkerJournalApi::StartModel);
        APISET().AddFunc<1>("FinishModel",&NativeWorkerJournalApi::FinishModel);
        APISET().AddFunc<4>("ModelEvent",&NativeWorkerJournalApi::ModelEvent);
        APISET().AddFunc<3>("ToolEvent",&NativeWorkerJournalApi::ToolEvent);
        APISET().AddFunc<3>("WorkspaceToolEvent",&NativeWorkerJournalApi::WorkspaceToolEvent);
        APISET().AddFunc<1>("CommitReadTurn",&NativeWorkerJournalApi::CommitReadTurn);
        APISET().AddFunc<1>("CommitWorkspaceTurn",&NativeWorkerJournalApi::CommitWorkspaceTurn);
        APISET().AddFunc<1>("Complete",&NativeWorkerJournalApi::Complete);
    END_PACKAGE
};
struct NativeWorkerReadApi {
    std::shared_ptr<NativeWorkerModelState> state;
    NativeWorkerReadApi()=default;
    explicit NativeWorkerReadApi(NativeWorkerApi*);
    X::Value Invoke(X::Value invocation);
    BEGIN_PACKAGE(NativeWorkerReadApi)
        APISET().AddFunc<1>("Invoke",&NativeWorkerReadApi::Invoke,X3_NATIVE_IPC_ARGS_BY_VALUE);
    END_PACKAGE
};
struct NativeWorkerFileApi {
    std::shared_ptr<NativeWorkerModelState> state;
    NativeWorkerFileApi()=default;
    explicit NativeWorkerFileApi(NativeWorkerApi*);
    X::Value Invoke(X::Value invocation);
    BEGIN_PACKAGE(NativeWorkerFileApi)
        APISET().AddFunc<1>("Invoke",&NativeWorkerFileApi::Invoke,X3_NATIVE_IPC_ARGS_BY_VALUE);
    END_PACKAGE
};
struct NativeWorkerProcessApi {
    std::shared_ptr<NativeWorkerModelState> state;
    NativeWorkerProcessApi()=default;
    explicit NativeWorkerProcessApi(NativeWorkerApi*);
    X::Value Invoke(X::Value invocation);
    X::Value Join(std::string attempt);
    BEGIN_PACKAGE(NativeWorkerProcessApi)
        APISET().AddFunc<1>("Invoke",&NativeWorkerProcessApi::Invoke,X3_NATIVE_IPC_ARGS_BY_VALUE);
        APISET().AddFunc<1>("Join",&NativeWorkerProcessApi::Join);
    END_PACKAGE
};
struct NativeWorkerRemoteApi {
    std::shared_ptr<NativeWorkerModelState> state;
    NativeWorkerRemoteApi()=default;
    explicit NativeWorkerRemoteApi(NativeWorkerApi*);
    X::Value Invoke(X::Value invocation);
    BEGIN_PACKAGE(NativeWorkerRemoteApi)
        APISET().AddFunc<1>("Invoke",&NativeWorkerRemoteApi::Invoke,X3_NATIVE_IPC_ARGS_BY_VALUE);
    END_PACKAGE
};
struct NativeWorkerCallableApi {
    std::shared_ptr<NativeWorkerModelState> state;
    NativeWorkerCallableApi()=default;
    explicit NativeWorkerCallableApi(NativeWorkerApi*);
    X::Value Invoke(X::Value invocation);
    X::Value Catalogue();
    BEGIN_PACKAGE(NativeWorkerCallableApi)
        APISET().AddFunc<1>("Invoke",&NativeWorkerCallableApi::Invoke,X3_NATIVE_IPC_ARGS_BY_VALUE);
        APISET().AddFunc<0>("Catalogue",&NativeWorkerCallableApi::Catalogue);
    END_PACKAGE
};
struct NativeWorkerApi {
    std::shared_ptr<NativeWorkerModelState> state;
    NativeWorkerApi()=default;
    NativeWorkerApi(NativeWorkersApi*,std::string ticket);
    X::Value Model();
    X::Value Journal();
    X::Value Reads();
    X::Value Files();
    X::Value Processes();
    X::Value Remotes();
    X::Value Callables();
    X::Value Planning();
    X::Value Identity();
    BEGIN_PACKAGE(NativeWorkerApi)
        APISET().AddFunc<0>("Identity",&NativeWorkerApi::Identity);
        APISET().AddFunc<0>("Model",&NativeWorkerApi::Model);
        APISET().AddFunc<0>("Journal",&NativeWorkerApi::Journal);
        APISET().AddFunc<0>("Reads",&NativeWorkerApi::Reads);
        APISET().AddFunc<0>("Files",&NativeWorkerApi::Files);
        APISET().AddFunc<0>("Processes",&NativeWorkerApi::Processes);
        APISET().AddFunc<0>("Remotes",&NativeWorkerApi::Remotes);
        APISET().AddFunc<0>("Callables",&NativeWorkerApi::Callables);
        APISET().AddFunc<0>("Planning",&NativeWorkerApi::Planning);
    END_PACKAGE
};
struct NativeWorkersApi {
    X::Value Open(std::string ticket);
    BEGIN_PACKAGE(NativeWorkersApi)
        APISET().AddFunc<1>("Open",&NativeWorkersApi::Open);
    END_PACKAGE
};
// Native SDK client, independent of the old worker packet/RPC implementation.
class NativeWorkerModelClient final:public AgentWorkerServices {
public:
    NativeWorkerModelClient(NativeWorkerModelAccess,std::string root,std::string owner,std::string workspace,std::string generation,std::int64_t revision);
    ~NativeWorkerModelClient();
    NativeWorkerModelClient(const NativeWorkerModelClient&)=delete;
    NativeWorkerModelClient& operator=(const NativeWorkerModelClient&)=delete;
    std::shared_ptr<DynamicPlanExecution> planning(std::chrono::steady_clock::time_point);
    AgentWorkerModelAdmission admission(std::stop_token={}) override;
    ModelRequest prepare_request(std::stop_token={}) override;
    ModelRequest rebuild_request(const std::string&,const ModelRequest&,std::stop_token={}) override;
    ModelCompletion complete(const std::string&,const ModelRequest&,ChatCompletionStream::Sink,std::stop_token={}) override;
    AgentCallableToolResult callable_invoke(const AgentCallableInvocation&,std::stop_token={}) override;
    std::vector<std::string> callable_names(std::stop_token={}) override;
    AgentRemoteToolResult remote_invoke(const AgentRemoteToolInvocation&,std::stop_token={}) override;
    AgentProcessToolResult process_invoke(const AgentProcessToolInvocation&,std::stop_token={}) override;
    bool process_join(const std::string& attempt,std::stop_token={}) override;
    AgentFileToolResult file_invoke(const AgentFileToolInvocation&,std::stop_token={}) override;
    AgentReadToolResult read_invoke(const AgentWorkspaceInvocation&,std::stop_token={}) override;
    std::string journal_run(std::string id) override;
    std::string journal_session(std::string id) override;
    std::string journal_history(std::string id) override;
    std::string journal_events(std::string id,std::int64_t after) override;
    std::string journal_root_budget(std::string id) override;
    std::string journal_child_admission(std::string id) override;
    std::string journal_workspace_mode() override;
    std::string journal_execution_window() override;
    std::string journal_begin() override;
    std::string journal_reserve_model(std::string attempt) override;
    std::string journal_reserve_planning_continuation(std::string call,std::string attempt) override;
    std::string journal_start_model(std::string attempt) override;
    std::string journal_finish_model(std::string attempt) override;
    std::string journal_model_event(std::string attempt,std::int64_t ordinal,std::string kind,std::string json) override;
    std::string journal_tool_event(std::string attempt,std::int64_t ordinal,bool returned) override;
    std::string journal_workspace_tool_event(std::string attempt,std::int64_t ordinal,bool returned) override;
    std::string journal_commit_read_turn(std::string attempt) override;
    std::string journal_commit_workspace_turn(std::string attempt) override;
    std::string journal_complete(std::string attempt) override;
private:struct Impl;std::shared_ptr<Impl> impl_;
};
}
