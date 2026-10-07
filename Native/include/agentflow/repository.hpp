#pragma once
#include "agentflow/store.hpp"
#include "agentflow/secret_protection.hpp"
#include "agentflow/operation.hpp"
#include <memory>
#include <vector>

namespace agentflow {
class GraphPlan;
struct GraphRootRecord {Run run;std::string graph_id;std::int64_t graph_revision;std::string specification_json;};
struct CredentialMetadata {
    std::string scope,id,purpose,label;
    std::int64_t revision;
};
// Target repository: C++ contracts, all database operations through xlang3.
// Construct/use/destroy on the persistence thread. No direct SQLite linkage.
class Repository {
public:
    Repository(const std::string& database,const std::vector<std::string>& import_roots);
    ~Repository();
    Repository(const Repository&)=delete;
    Repository& operator=(const Repository&)=delete;
    Session create_session(const std::string& id,const std::string& title);
    Session session(const std::string& id);
    std::vector<Session> sessions();
    Run create_run(const std::string& id,const std::string& session_id);
    Run start_prompt_run(const std::string& id,const std::string& session_id,const std::string& prompt_json);
    Run start_graph_run(const std::string& id,const std::string& session_id,const std::string& graph_id,std::int64_t revision,const GraphPlan& plan,const std::string& prompt_json);
    GraphRootRecord graph_run(const std::string& id);
    Run start_graph_child(const std::string& id,const std::string& parent_id,const std::string& node_id,const std::string& prompt_json);
    std::vector<Run> children(const std::string& parent_id);
    std::vector<Message> run_history(const std::string& id);
    std::vector<Event> graph_events(const std::string& id,std::int64_t after=0);
    void append_user_message(const std::string& session_id,const std::string& json);
    void record_tool_turn(const std::string& run_id,const std::string& assistant_json,const std::vector<std::string>& tool_json);
    Run complete_run(const std::string& run_id,const std::string& assistant_json);
    Run run(const std::string& id);
    std::vector<Run> runs(const std::string& session_id);
    Run transition(const std::string& id,RunState expected,RunState next,const std::string& json="{}");
    Event append_event(const std::string& id,const std::string& kind,const std::string& json);
    std::vector<Event> events(const std::string& id,std::int64_t after=0);
    void append_message(const std::string& id,const std::string& role,const std::string& json);
    std::vector<Message> history(const std::string& id);
    void put_information(const std::string& category,const std::string& id,const std::string& json);
    std::string information(const std::string& category,const std::string& id);
    // Effect journal and one-operation permissions. Backend callers authorize
    // controller access before deciding; only an owning executor claims/finishes.
    Operation request_operation(const std::string& id,const OperationSpec& spec,std::int64_t expires_unix_ms);
    Operation operation(const std::string& id);
    std::vector<Operation> operations(const std::string& run_id);
    // Actor is derived from the backend's authenticated controller context.
    Operation decide_operation(const std::string& id,OperationDecision decision,const std::string& actor);
    Operation claim_operation(const std::string& id,const OperationSpec& actual);
    Operation cancel_operation(const std::string& id,const OperationSpec& actual);
    Operation expire_operation(const std::string& id);
    Operation finish_operation(const std::string& id,OperationState outcome,const std::string& result_json);
    // Backend-internal operations. Service must authorize scope before calling.
    // expected_revision=0 creates; positive revisions rotate with stale-write rejection.
    // Deleted identities are retired permanently to prevent stale-reference reuse.
    CredentialMetadata put_credential(const std::string& scope,const std::string& id,
        const std::string& purpose,const std::string& label,const SecretBytes& secret,
        std::int64_t expected_revision);
    std::vector<CredentialMetadata> credentials(const std::string& scope);
    SecretBytes resolve_credential(const std::string& scope,const std::string& id,
        const std::string& purpose);
    void delete_credential(const std::string& scope,const std::string& id,std::int64_t expected_revision);
    std::size_t recover_interrupted(const BackendLease& owner);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
