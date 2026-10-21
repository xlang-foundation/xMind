#pragma once
#include "agentflow/agent_worker_model_host.hpp"
#include "agentflow/agent_read_tool_host.hpp"
#include "agentflow/agent_file_tool_host.hpp"
#include "agentflow/agent_worker_process_service.hpp"
#include "xlang3/xlang3.h"
#include <memory>
namespace agentflow {
class AgentWorkerJournalHost;
struct NativeWorkerModelState;
struct NativeWorkerModelAccess {std::string endpoint,ticket;std::uint32_t pid=0;};
class NativeWorkerModelBinding final {
public:
    explicit NativeWorkerModelBinding(AgentWorkerModelHost&,AgentWorkerJournalHost* journal=nullptr,AgentReadToolHost* reads=nullptr,AgentFileToolHost* files=nullptr,AgentWorkerProcessService* processes=nullptr);
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
struct NativeWorkerApi {
    std::shared_ptr<NativeWorkerModelState> state;
    NativeWorkerApi()=default;
    NativeWorkerApi(NativeWorkersApi*,std::string ticket);
    X::Value Model();
    X::Value Journal();
    X::Value Reads();
    X::Value Files();
    X::Value Processes();
    X::Value Identity();
    BEGIN_PACKAGE(NativeWorkerApi)
        APISET().AddFunc<0>("Identity",&NativeWorkerApi::Identity);
        APISET().AddFunc<0>("Model",&NativeWorkerApi::Model);
        APISET().AddFunc<0>("Journal",&NativeWorkerApi::Journal);
        APISET().AddFunc<0>("Reads",&NativeWorkerApi::Reads);
        APISET().AddFunc<0>("Files",&NativeWorkerApi::Files);
        APISET().AddFunc<0>("Processes",&NativeWorkerApi::Processes);
    END_PACKAGE
};
struct NativeWorkersApi {
    X::Value Open(std::string ticket);
    BEGIN_PACKAGE(NativeWorkersApi)
        APISET().AddFunc<1>("Open",&NativeWorkersApi::Open);
    END_PACKAGE
};
// Native SDK client, independent of the old worker packet/RPC implementation.
class NativeWorkerModelClient final {
public:
    NativeWorkerModelClient(NativeWorkerModelAccess,std::string root,std::string owner,std::string workspace,std::string generation,std::int64_t revision);
    ~NativeWorkerModelClient();
    AgentWorkerModelAdmission admission(std::stop_token={});
    ModelRequest prepare_request(std::stop_token={});
    ModelRequest rebuild_request(const std::string&,const ModelRequest&,std::stop_token={});
    ModelCompletion complete(const std::string&,const ModelRequest&,ChatCompletionStream::Sink,std::stop_token={});
    AgentProcessToolResult process_invoke(const AgentProcessToolInvocation&,std::stop_token={});
    bool process_join(const std::string& attempt,std::stop_token={});
    AgentFileToolResult file_invoke(const AgentFileToolInvocation&,std::stop_token={});
    AgentReadToolResult read_invoke(const AgentWorkspaceInvocation&,std::stop_token={});
    std::string journal_run(std::string id);
    std::string journal_session(std::string id);
    std::string journal_history(std::string id);
    std::string journal_events(std::string id,std::int64_t after);
    std::string journal_root_budget(std::string id);
    std::string journal_child_admission(std::string id);
    std::string journal_workspace_mode();
    std::string journal_execution_window();
    std::string journal_begin();
    std::string journal_reserve_model(std::string attempt);
    std::string journal_start_model(std::string attempt);
    std::string journal_finish_model(std::string attempt);
    std::string journal_model_event(std::string attempt,std::int64_t ordinal,std::string kind,std::string json);
    std::string journal_tool_event(std::string attempt,std::int64_t ordinal,bool returned);
    std::string journal_workspace_tool_event(std::string attempt,std::int64_t ordinal,bool returned);
    std::string journal_commit_read_turn(std::string attempt);
    std::string journal_commit_workspace_turn(std::string attempt);
    std::string journal_complete(std::string attempt);
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
