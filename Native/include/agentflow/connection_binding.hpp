#pragma once
#include "agentflow/records.hpp"
#include "agentflow/operation.hpp"
#include "agentflow/run_executor.hpp"
#include <memory>
namespace agentflow {
class PersistenceService;class RunExecutor;class GraphExecution;class BackendOwnerControl;struct LocalConnectionContext;
struct ConnectionBindingIdentity {
    std::string connection_id,server_id,backend_generation;
    std::int64_t profile_revision=0,protocol_version=1;
};
struct ConnectionEventCursor {
    std::string server_id,connection_id,session_id,run_id;
    std::int64_t sequence=0;
    bool operator==(const ConnectionEventCursor&)const=default;
};
struct ConnectionEventPage {std::vector<Event> events;ConnectionEventCursor cursor;};
struct BoundConnectionSession {ConnectionBindingIdentity binding;Session session;};
struct BoundConnectionMessages {ConnectionBindingIdentity binding;std::string session_id;MessagePage page;};
struct BoundConnectionRunMessages {ConnectionBindingIdentity binding;std::string session_id,run_id;MessagePage page;};
struct BoundConnectionChildHistory {ConnectionBindingIdentity binding;std::string session_id,parent_id,child_id;std::vector<Message> messages;};
struct BoundConnectionRun {ConnectionBindingIdentity binding;Run run;};
struct BoundConnectionOperation {ConnectionBindingIdentity binding;Operation operation;};
struct BoundConnectionOperations {ConnectionBindingIdentity binding;std::string session_id,run_id;std::vector<Operation> operations;};
class CapturedRunOperations {
public:
    BoundConnectionOperations list()const;
    BoundConnectionOperation open(const std::string& id)const;
    BoundConnectionOperation decide(const std::string& id,OperationDecision decision)const;
private:
    friend class CapturedLocalConnection;
    CapturedRunOperations()=default;
    std::shared_ptr<LocalConnectionContext> context_;
    ConnectionBindingIdentity identity_;
    std::string session_,run_;
};
struct ConnectionRunRequest {
    std::string id,session,prompt,model;
    std::optional<std::string> inbox;
    std::vector<PromptFileAttachment> files;
    std::optional<WorkspaceAdmission> workspace;
    std::optional<ProviderProfileAdmission> provider;
};
struct ConnectionGraphRequest {ConnectionRunRequest run;std::string graph;std::int64_t revision=0;};
Run submit_local_connection_run(RunExecutor&,ConnectionRunRequest);
class ConnectionEventStream {
public:
    ConnectionEventCursor cursor()const;
    ConnectionEventPage read(ConnectionEventCursor cursor)const;
private:
    friend class CapturedLocalConnection;
    ConnectionEventStream()=default;
    std::shared_ptr<LocalConnectionContext> context_;
    ConnectionBindingIdentity identity_;
    std::string session_,root_;
};
class CapturedLocalConnection {
public:
    ConnectionBindingIdentity identity()const;
    BoundConnectionSession open_session(std::string id)const;
    BoundConnectionSession create_session(std::string id,std::string title)const;
    BoundConnectionMessages messages(std::string session,std::int64_t before_sequence,std::int64_t count)const;
    BoundConnectionRun open_run(std::string id)const;
    BoundConnectionRunMessages run_messages(std::string session,std::string run,std::int64_t before_sequence,std::int64_t count)const;
    BoundConnectionChildHistory child_history(std::string session,std::string parent,std::string child)const;
    BoundConnectionRun start_run(std::string id,std::string session,std::string prompt)const;
    BoundConnectionRun start_run(ConnectionRunRequest request)const;
    BoundConnectionRun start_graph(ConnectionGraphRequest request)const;
    void cancel_run(const std::string& id)const;
    std::shared_ptr<CapturedRunOperations> operations(std::string session,std::string run)const;
    std::shared_ptr<ConnectionEventStream> events(std::string session,std::string root)const;
private:
    friend class LocalConnectionBindings;
    CapturedLocalConnection()=default;
    std::shared_ptr<LocalConnectionContext> context_;
    ConnectionBindingIdentity identity_;
};
// Native owner facade. Returned objects retain state, but close fences all of
// them before the backend's borrowed persistence/executor/control are destroyed.
class LocalConnectionBindings {
public:
    LocalConnectionBindings(PersistenceService&,RunExecutor*,BackendOwnerControl* = nullptr,GraphExecution* = nullptr);
    ~LocalConnectionBindings();
    LocalConnectionBindings(const LocalConnectionBindings&)=delete;
    LocalConnectionBindings& operator=(const LocalConnectionBindings&)=delete;
    std::shared_ptr<CapturedLocalConnection> open(std::string id,std::int64_t profile_revision);
    void close();
private:
    std::shared_ptr<LocalConnectionContext> context_;
};
}
