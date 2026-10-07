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
