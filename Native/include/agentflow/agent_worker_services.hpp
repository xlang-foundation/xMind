#pragma once
#include "agentflow/agent_worker_model_host.hpp"
#include "agentflow/agent_read_tool_host.hpp"
#include "agentflow/agent_file_tool_host.hpp"
#include "agentflow/agent_worker_process_service.hpp"
#include "agentflow/agent_remote_tool_host.hpp"
#include "agentflow/agent_worker_callable_service.hpp"
#include <memory>
#include <chrono>
namespace agentflow {
// Typed worker ports. Product implementations use SDK remote objects;
// packet transport implementations are confined to test fixtures.
class AgentWorkerServices {
public:
    virtual ~AgentWorkerServices()=default;
    virtual AgentWorkerModelAdmission admission(std::stop_token={})=0;
    virtual ModelRequest prepare_request(std::stop_token={})=0;
    virtual ModelRequest rebuild_request(const std::string&,const ModelRequest&,std::stop_token={})=0;
    virtual ModelCompletion complete(const std::string&,const ModelRequest&,ChatCompletionStream::Sink,std::stop_token={})=0;
    virtual AgentCallableToolResult callable_invoke(const AgentCallableInvocation&,std::stop_token={})=0;
    virtual std::vector<std::string> callable_names(std::stop_token={})=0;
    virtual AgentRemoteToolResult remote_invoke(const AgentRemoteToolInvocation&,std::stop_token={})=0;
    virtual AgentProcessToolResult process_invoke(const AgentProcessToolInvocation&,std::stop_token={})=0;
    virtual bool process_join(const std::string& attempt,std::stop_token={})=0;
    virtual AgentFileToolResult file_invoke(const AgentFileToolInvocation&,std::stop_token={})=0;
    virtual AgentReadToolResult read_invoke(const AgentWorkspaceInvocation&,std::stop_token={})=0;
    virtual std::string journal_run(std::string id)=0;
    virtual std::string journal_session(std::string id)=0;
    virtual std::string journal_history(std::string id)=0;
    virtual std::string journal_events(std::string id,std::int64_t after)=0;
    virtual std::string journal_root_budget(std::string id)=0;
    virtual std::string journal_child_admission(std::string id)=0;
    virtual std::string journal_workspace_mode()=0;
    virtual std::string journal_execution_window()=0;
    virtual std::string journal_begin()=0;
    virtual std::string journal_reserve_model(std::string attempt)=0;
    virtual std::string journal_reserve_planning_continuation(std::string call,std::string attempt)=0;
    virtual std::string journal_start_model(std::string attempt)=0;
    virtual std::string journal_finish_model(std::string attempt)=0;
    virtual std::string journal_model_event(std::string attempt,std::int64_t ordinal,std::string kind,std::string json)=0;
    virtual std::string journal_tool_event(std::string attempt,std::int64_t ordinal,bool returned)=0;
    virtual std::string journal_workspace_tool_event(std::string attempt,std::int64_t ordinal,bool returned)=0;
    virtual std::string journal_commit_read_turn(std::string attempt)=0;
    virtual std::string journal_commit_workspace_turn(std::string attempt)=0;
    virtual std::string journal_complete(std::string attempt)=0;
};
}
