#pragma once
#include "agentflow/repository.hpp"
#include <future>

namespace agentflow {
struct PersistenceClosed : std::runtime_error {using std::runtime_error::runtime_error;};
struct PersistenceBusy : std::runtime_error {using std::runtime_error::runtime_error;};

// Thread-safe request boundary for server/agent workers. The repository, runtime
// and lease exist only on its private worker. Results contain owned C++ values.
// Scope authorization belongs to the calling backend service.
class PersistenceService {
public:
    PersistenceService(std::string database,std::vector<std::string> import_roots,
        std::size_t max_pending=1024);
    ~PersistenceService();
    PersistenceService(const PersistenceService&)=delete;
    PersistenceService& operator=(const PersistenceService&)=delete;
    // Stop accepting, drain accepted requests, join and release ownership.
    // Safe for concurrent callers; the object must outlive all callers.
    void close();
    std::future<Session> create_session(std::string id,std::string title);
    std::future<Session> session(std::string id);
    std::future<std::vector<Session>> sessions();
    std::future<Run> create_run(std::string id,std::string session_id);
    std::future<Run> start_prompt_run(std::string id,std::string session_id,std::string prompt_json);
    std::future<Run> start_graph_run(std::string id,std::string session_id,std::string graph_id,std::int64_t revision,GraphPlan plan,std::string prompt_json);
    std::future<GraphRootRecord> graph_run(std::string id);
    std::future<Run> start_graph_child(std::string id,std::string parent_id,std::string node_id,std::string prompt_json,std::int64_t expected_checkpoint_revision=0);
    std::future<GraphRootRecord> settle_graph_child(std::string child_id,std::int64_t expected_checkpoint_revision=0);
    std::future<GraphRootRecord> start_graph_human(std::string id,std::string node_id,std::int64_t expected_checkpoint_revision);
    std::future<GraphRootRecord> input_graph_human(std::string id,std::string node_id,std::string input_json,std::string actor,std::int64_t expected_checkpoint_revision);
    std::future<GraphRootRecord> skip_graph_node(std::string id,std::string node_id,std::int64_t expected_checkpoint_revision);
    std::future<Run> retire_graph_run(std::string id,RunState terminal_state,std::string reason_json);
    std::future<std::vector<Run>> children(std::string parent_id);
    std::future<std::vector<Message>> run_history(std::string id);
    std::future<std::vector<Event>> graph_events(std::string id,std::int64_t after=0);
    std::future<void> append_user_message(std::string session_id,std::string json);
    std::future<void> record_tool_turn(std::string id,std::string assistant_json,std::vector<std::string> tool_json);
    std::future<Run> complete_run(std::string id,std::string assistant_json);
    std::future<Run> run(std::string id);
    std::future<std::vector<Run>> runs(std::string session_id);
    std::future<Run> transition(std::string id,RunState expected,RunState next,std::string json="{}");
    std::future<Event> append_event(std::string id,std::string kind,std::string json);
    std::future<std::vector<Event>> events(std::string id,std::int64_t after=0);
    std::future<void> append_message(std::string id,std::string role,std::string json);
    std::future<std::vector<Message>> history(std::string id);
    std::future<void> put_information(std::string category,std::string id,std::string json);
    std::future<std::string> information(std::string category,std::string id);
    std::future<Operation> request_operation(std::string id,OperationSpec spec,std::int64_t expires_unix_ms);
    std::future<Operation> operation(std::string id);
    std::future<std::vector<Operation>> operations(std::string run_id);
    std::future<Operation> decide_operation(std::string id,OperationDecision decision,std::string actor);
    std::future<Operation> claim_operation(std::string id,OperationSpec actual);
    std::future<Operation> cancel_operation(std::string id,OperationSpec actual);
    std::future<Operation> expire_operation(std::string id);
    std::future<Operation> finish_operation(std::string id,OperationState outcome,std::string result_json);
    std::future<CredentialMetadata> put_credential(std::string scope,std::string id,
        std::string purpose,std::string label,SecretBytes secret,std::int64_t expected_revision);
    std::future<std::vector<CredentialMetadata>> credentials(std::string scope);
    std::future<SecretBytes> resolve_credential(std::string scope,std::string id,std::string purpose);
    std::future<void> delete_credential(std::string scope,std::string id,std::int64_t expected_revision);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
