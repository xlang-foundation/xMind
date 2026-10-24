#pragma once
#include "agentflow/records.hpp"
#include "agentflow/operation.hpp"
#include "agentflow/run_executor.hpp"
#include "agentflow/graph_service.hpp"
#include "agentflow/provider_profile_setup.hpp"
#include "agentflow/agent_permission_grants.hpp"
#include <memory>
#include <algorithm>
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
struct ConnectionModelSelectionRequest {std::optional<std::string> model;ProviderProfileAdmission provider;std::int64_t revision=0;};
struct BoundConnectionModel {ConnectionBindingIdentity binding;std::string session_id;SessionModelState state;};
struct ConnectionCommandCatalogue {ExecutionWorkspaceMetadata workspace;std::string json;};
struct ConnectionCommandRequest {std::string id,session,command,arguments,model,graph;std::int64_t graph_revision=0;WorkspaceAdmission workspace;std::optional<ProviderProfileAdmission> provider;};
struct BoundConnectionCommands {ConnectionBindingIdentity binding;ConnectionCommandCatalogue catalogue;};
ConnectionCommandCatalogue read_local_connection_commands(RunExecutor&);
Run submit_local_connection_command(PersistenceService&,RunExecutor&,GraphExecution*,ConnectionCommandRequest);
struct ConnectionFolderScope {ConnectionBindingIdentity binding;std::string workspace_id,folder_id,path;std::int64_t revision=0;};
struct BoundConnectionFiles {ConnectionFolderScope scope;WorkspaceListing listing;};
struct BoundConnectionFile {ConnectionFolderScope scope;WorkspaceFile file;};
struct ConnectionFindRequest {std::string query,path=".",type="file";std::size_t limit=100;bool hidden=false,respect_ignore=true;};
struct BoundConnectionFind {ConnectionFolderScope scope;WorkspaceFind found;};
struct BoundConnectionWorkspace {ConnectionBindingIdentity binding;ExecutionWorkspaceMetadata workspace;};
struct BoundConnectionMembership {ConnectionBindingIdentity binding;WorkspaceMembershipSnapshot membership;};
struct BoundConnectionProviderModels {ConnectionBindingIdentity binding;std::vector<std::string> models;};
struct BoundConnectionProviders {ConnectionBindingIdentity binding;ProviderProfileRuntimeMetadata configuration;std::vector<ProviderProfileRouteMetadata> routes;};
struct BoundConnectionModels {ConnectionBindingIdentity binding;std::vector<std::string> models;};
struct BoundConnectionContext {ConnectionBindingIdentity binding;ContextControlSnapshot context;};
struct BoundConnectionContextRequest {ConnectionBindingIdentity binding;std::string session_id;ContextManualStatus request;};
struct ConnectionContextRequest {std::string id,model;std::int64_t head=0;std::optional<ProviderProfileAdmission> provider;};
struct BoundConnectionSkills {ConnectionBindingIdentity binding;WorkspaceSkillCatalogue catalogue;};
struct BoundConnectionSessionSkills {ConnectionBindingIdentity binding;std::string session_id;WorkspaceSessionSkills skills;};
struct ConnectionSkillSelectionRequest {std::vector<std::string> ids;std::int64_t revision=0;WorkspaceAdmission workspace;};
ContextManualStatus request_local_connection_context(RunExecutor&,const std::string&,ConnectionContextRequest);
struct BoundConnectionAgents {ConnectionBindingIdentity binding;std::vector<AgentDefinitionMetadata> agents;};
struct BoundConnectionAgentDefinition {ConnectionBindingIdentity binding;AgentDefinition agent;};
struct BoundConnectionSessionAgent {ConnectionBindingIdentity binding;std::string session_id;SessionAgentState state;};
struct ConnectionAgentSelectionRequest {std::optional<std::string> id;std::int64_t agent_revision=0,revision=0;};
inline SessionAgentState select_local_connection_agent(RunExecutor& executor,const std::string& session,ConnectionAgentSelectionRequest request){
    if(!executor.supports_agent_selection())throw RunUnavailable("Conversation agent selection is unavailable");
    if(request.revision<0||request.revision>=9007199254740991LL||request.agent_revision<0||request.agent_revision>9007199254740991LL)throw std::invalid_argument("Invalid agent selection revision");
    std::optional<AgentDefinitionMetadata> selected;
    if(request.id){if(request.id->empty()||request.id->size()>64||request.id->find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos||request.agent_revision<1)throw std::invalid_argument("Invalid named agent selection");
        const auto catalog=executor.agent_definitions();const auto found=std::find_if(catalog.begin(),catalog.end(),[&](const auto& item){return item.id==*request.id;});if(found==catalog.end()||found->revision!=request.agent_revision)throw Conflict("Named agent definition changed; refresh before selecting it");selected=*found;
    }else if(request.agent_revision!=0)throw std::invalid_argument("Clearing agent selection requires definition revision zero");
    return executor.select_session_agent(session,std::move(selected),request.revision);
}
struct BoundConnectionRun {ConnectionBindingIdentity binding;Run run;};
struct BoundConnectionOperation {ConnectionBindingIdentity binding;Operation operation;};
struct BoundConnectionOperations {ConnectionBindingIdentity binding;std::string session_id,run_id;std::vector<Operation> operations;};
struct BoundConnectionPermissions {ConnectionBindingIdentity binding;std::string session_id;std::vector<AgentPermissionPendingRequest> requests;};
struct BoundConnectionPermissionReply {ConnectionBindingIdentity binding;std::string session_id;Operation operation;};
struct BoundConnectionPermissionGrants {ConnectionBindingIdentity binding;std::string session_id;AgentPermissionGrantSnapshot snapshot;};
class CapturedSessionPermissions {
public:
    BoundConnectionPermissions pending()const;
    BoundConnectionPermissionReply reply(std::string id,OperationDecision decision,bool grant_resource)const;
    BoundConnectionPermissionGrants grants()const;
    BoundConnectionPermissionGrants revoke(std::string id,std::int64_t revision)const;
private:
    friend class CapturedLocalConnection;
    CapturedSessionPermissions()=default;
    std::shared_ptr<LocalConnectionContext> context_;
    ConnectionBindingIdentity identity_;
    std::string session_;
};
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
struct ConnectionGraphSnapshot {GraphRootRecord record;GraphContextMetadata context;};
struct BoundConnectionGraphs {ConnectionBindingIdentity binding;std::vector<GraphExecutionMetadata> graphs;};
struct BoundConnectionGraph {ConnectionBindingIdentity binding;ConnectionGraphSnapshot graph;};
struct BoundConnectionGraphUpdate {ConnectionBindingIdentity binding;GraphRootRecord graph;};
struct BoundConnectionGraphChildren {ConnectionBindingIdentity binding;std::string root_id,session_id;std::vector<Run> children;};
ConnectionGraphSnapshot read_local_connection_graph(PersistenceService&,GraphExecution&,const std::string&);
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
    bool tree_=true;
};
class CapturedLocalConnection {
public:
    ConnectionBindingIdentity identity()const;
    BoundConnectionModels models()const;
    BoundConnectionContext context(const std::string& session,const std::string& model)const;
    BoundConnectionContextRequest context_request(const std::string& session,const std::string& id,const std::string& model)const;
    BoundConnectionContextRequest compact_context(const std::string& session,ConnectionContextRequest request)const;
    BoundConnectionSkills skills()const;
    BoundConnectionSessionSkills session_skills(const std::string& session)const;
    BoundConnectionSessionSkills select_session_skills(const std::string& session,ConnectionSkillSelectionRequest request)const;
    BoundConnectionAgents agents()const;
    BoundConnectionAgentDefinition agent_definition(const std::string& id)const;
    BoundConnectionSessionAgent session_agent(const std::string& session)const;
    BoundConnectionSessionAgent select_session_agent(const std::string& session,ConnectionAgentSelectionRequest request)const;
    BoundConnectionProviders providers()const;
    BoundConnectionProviders save_provider(std::string id,std::string route,std::string model,SecretBytes key,std::int64_t revision,bool activate)const;
    BoundConnectionProviderModels discover_provider_models(std::string id,std::string route,SecretBytes key,std::int64_t revision)const;
    BoundConnectionProviders import_providers(std::string path,std::int64_t revision)const;
    BoundConnectionProviders select_provider(std::string id,std::int64_t revision)const;
    BoundConnectionWorkspace workspace()const;
    BoundConnectionMembership membership()const;
    BoundConnectionFiles files(std::string folder,std::int64_t revision,std::string path)const;
    BoundConnectionFile file(std::string folder,std::int64_t revision,std::string path)const;
    BoundConnectionFind find(std::string folder,std::int64_t revision,ConnectionFindRequest request)const;
    BoundConnectionCommands commands()const;
    BoundConnectionRun invoke_command(ConnectionCommandRequest request)const;
    BoundConnectionModel session_model(const std::string& session)const;
    BoundConnectionModel select_session_model(const std::string& session,ConnectionModelSelectionRequest request)const;
    BoundConnectionSession open_session(std::string id)const;
    BoundConnectionSession create_session(std::string id,std::string title)const;
    BoundConnectionMessages messages(std::string session,std::int64_t before_sequence,std::int64_t count)const;
    BoundConnectionRun open_run(std::string id)const;
    BoundConnectionRunMessages run_messages(std::string session,std::string run,std::int64_t before_sequence,std::int64_t count)const;
    BoundConnectionChildHistory child_history(std::string session,std::string parent,std::string child)const;
    BoundConnectionRun start_run(std::string id,std::string session,std::string prompt)const;
    BoundConnectionRun start_run(ConnectionRunRequest request)const;
    BoundConnectionRun start_graph(ConnectionGraphRequest request)const;
    BoundConnectionGraphs graphs()const;
    BoundConnectionGraph graph(const std::string& id)const;
    BoundConnectionGraphChildren graph_children(const std::string& id)const;
    BoundConnectionGraphUpdate graph_input(const std::string& root,const std::string& owner,const std::string& node,const std::string& input,std::int64_t revision,std::int64_t owner_revision=0)const;
    BoundConnectionRun resume_graph(const std::string& root,std::int64_t revision)const;
    void cancel_run(const std::string& id)const;
    std::shared_ptr<CapturedSessionPermissions> permissions(std::string session)const;
    std::shared_ptr<CapturedRunOperations> operations(std::string session,std::string run)const;
    std::shared_ptr<ConnectionEventStream> events(std::string session,std::string root)const;
    std::shared_ptr<ConnectionEventStream> run_events(std::string session,std::string run)const;
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
