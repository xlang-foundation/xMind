#pragma once
#include "agentflow/process.hpp"
#include "agentflow/permission_waiter.hpp"
#include "agentflow/persistence_service.hpp"
#include "agentflow/workspace_tools.hpp"
#include "agentflow/instruction_precondition.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace agentflow {
struct ProcessOutcomeUnrecorded : std::runtime_error {using std::runtime_error::runtime_error;};
struct ProcessProfile {
    std::string id,executable;
    std::int64_t revision;
    std::vector<std::string> prefix_arguments;
    std::chrono::milliseconds max_timeout{120000};
    std::string executable_id;
    bool auto_approve=false;
};
// Backend-owned process-tool capability consumed by AgentRunner. This keeps
// process tool discovery, execution, joins and cancellation behind one host
// seam. Workspace handles and instruction verifiers remain native backend
// capabilities; this interface is not an IPC wire contract.
class AgentProcessToolHost {
public:
    virtual ~AgentProcessToolHost()=default;
    virtual ModelToolDefinition definition() const=0;
    virtual ModelToolDefinition wait_definition() const=0;
    virtual ModelToolDefinition stop_definition() const=0;
    virtual const std::vector<ProcessProfile>& profiles() const noexcept=0;
    virtual std::string invoke(const std::string& operation_id,const std::string& run_id,
        const std::string& arguments_json,std::int64_t expires_unix_ms,std::stop_token cancel={},
        InstructionPrecondition guidance={},bool allow_background=false)=0;
    virtual std::string invoke_in_workspace(const std::string& operation_id,const std::string& run_id,
        const std::string& arguments_json,std::int64_t expires_unix_ms,const WorkspaceToolSet& membership,
        std::size_t folder_index,std::stop_token cancel={},InstructionPrecondition guidance={},
        bool allow_background=true)=0;
    virtual std::string wait(const std::string& run_id,const std::string& arguments_json,std::stop_token cancel={})=0;
    virtual std::string stop(const std::string& run_id,const std::string& arguments_json,std::stop_token cancel={})=0;
    virtual std::vector<std::string> join_run(const std::string& run_id,std::stop_token cancel={})=0;
};
// Backend-owned immutable profile snapshot. The model chooses a registered ID
// and literal arguments, never an executable, environment, identity or grant.
// Normal/nonzero exits acknowledge process lifecycle, not independent effects.
// Interrupted dispatch remains uncertain; unrecorded outcomes require fail-stop.
class ProcessExecutor : public AgentProcessToolHost {
public:
    ProcessExecutor(PersistenceService& store,WorkspaceTools& workspace,
        std::string workspace_root,std::vector<ProcessProfile> profiles);
    ~ProcessExecutor() override;
    ModelToolDefinition definition() const override;
    ModelToolDefinition wait_definition() const override;
    // Backend-private immutable bindings, including resolved executable IDs.
    const std::vector<ProcessProfile>& profiles() const noexcept override{return profiles_;}
    std::string invoke(const std::string& operation_id,const std::string& run_id,
        const std::string& arguments_json,std::int64_t expires_unix_ms,std::stop_token cancel={},InstructionPrecondition guidance={},bool allow_background=false) override;
    std::string invoke_in_workspace(const std::string& operation_id,const std::string& run_id,
        const std::string& arguments_json,std::int64_t expires_unix_ms,const WorkspaceToolSet& membership,
        std::size_t folder_index,std::stop_token cancel={},InstructionPrecondition guidance={},bool allow_background=true) override;
    std::string wait(const std::string& run_id,const std::string& arguments_json,std::stop_token cancel={}) override;
    ModelToolDefinition stop_definition() const override;
    std::string stop(const std::string& run_id,const std::string& arguments_json,std::stop_token cancel={}) override;
    std::vector<std::string> join_run(const std::string& run_id,std::stop_token cancel={}) override;
private:
    struct Worker {std::string id;std::shared_ptr<std::atomic_bool> done;std::shared_ptr<std::atomic_bool> intentional_stop;std::jthread thread;};
    void reap_workers();
    std::string invoke_scoped(const std::string& operation_id,const std::string& run_id,
        const std::string& arguments_json,std::int64_t expires_unix_ms,WorkspaceTools& workspace,
        const std::string& root,std::string workspace_id,std::string folder_id,std::int64_t workspace_revision,
        bool primary_folder,std::stop_token cancel,InstructionPrecondition guidance,bool allow_background);
    std::string execute_claimed(const std::string& operation_id,const std::string& run_id,const std::string& profile_id,
        ProcessConfiguration launch,std::stop_token cancel,bool persistent=false,
        std::shared_ptr<std::atomic_bool> intentional_stop={});
    PersistenceService& store_;
    WorkspaceTools& workspace_;
    std::string root_;
    std::vector<ProcessProfile> profiles_;
    std::mutex workers_mutex_;
    std::vector<Worker> workers_;
};
}
