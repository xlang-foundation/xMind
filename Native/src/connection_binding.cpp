#include "agentflow/connection_binding.hpp"
#include "agentflow/connection_profiles.hpp"
#include "agentflow/backend_owner_control.hpp"
#include "agentflow/run_executor.hpp"
#include "agentflow/graph_service.hpp"
#include <algorithm>
#include <random>
namespace agentflow {
struct LocalConnectionContext {
    PersistenceService& store;RunExecutor* executor;GraphExecution* graphs;BackendOwnerControl* owner;
    ConnectionProfiles profiles;std::string generation;
    mutable std::shared_mutex lifetime;bool active=true;
    LocalConnectionContext(PersistenceService& persistence,RunExecutor* execution,BackendOwnerControl* control,GraphExecution* graph)
        :store(persistence),executor(execution),graphs(graph?graph:dynamic_cast<GraphExecution*>(execution)),owner(control),profiles(persistence){
        if(owner)generation=owner->status().generation;
        else{std::random_device random;for(int i=0;i<4;++i)generation+=std::to_string(random())+"-";}
    }
    template<class Read> auto folder_read(const std::string& folder,std::int64_t revision,Read read)const{
        if(!executor||!executor->supports_workspace_browsing())throw RunUnavailable("Workspace browsing requires an opened root");if(revision<1||revision>9007199254740991LL)throw std::invalid_argument("Invalid workspace revision");const auto before=executor->execution_workspace();const auto snapshot=executor->workspace_membership_snapshot();validate_workspace_admission({before.workspace_id,before.authority_id},executor->execution_workspace());if(snapshot.workspace_id!=before.workspace_id||snapshot.revision!=revision)throw Conflict("Workspace membership changed before read");WorkspaceToolSet tools(snapshot.workspace_id,snapshot.revision,snapshot.folders);auto result=read(snapshot,tools.folder_tools(tools.folder_index(folder)));const auto after=executor->workspace_membership_snapshot();validate_workspace_admission({before.workspace_id,before.authority_id},executor->execution_workspace());if(after.workspace_id!=snapshot.workspace_id||after.revision!=snapshot.revision||after.folders.size()!=snapshot.folders.size()||!std::equal(after.folders.begin(),after.folders.end(),snapshot.folders.begin(),[](const auto& a,const auto& b){return a.folder_id==b.folder_id&&a.root==b.root;}))throw Conflict("Workspace membership changed during read");return result;
    }
    auto enter()const{std::shared_lock lock(lifetime);if(!active)throw RunUnavailable("Captured connection backend retired");if(owner&&owner->status().generation!=generation)throw Conflict("Captured backend generation changed");return lock;}
    std::optional<std::shared_lock<std::shared_mutex>> admit(){if(owner)return owner->admit();return {};}
};
LocalConnectionBindings::LocalConnectionBindings(PersistenceService& store,RunExecutor* executor,BackendOwnerControl* owner,GraphExecution* graph)
    :context_(std::make_shared<LocalConnectionContext>(store,executor,owner,graph)){}
LocalConnectionBindings::~LocalConnectionBindings(){close();}
void LocalConnectionBindings::close(){std::unique_lock lock(context_->lifetime);context_->active=false;}
std::shared_ptr<CapturedLocalConnection> LocalConnectionBindings::open(std::string id,std::int64_t revision){
    auto life=context_->enter();auto admission=context_->admit();const auto catalog=context_->profiles.load_or_initialize();
    const auto selected=std::find_if(catalog.profiles.begin(),catalog.profiles.end(),[&](const auto& profile){return profile.id==id;});
    if(selected==catalog.profiles.end())throw NotFound("Connection profile not found");
    if(selected->kind!=ConnectionKind::local)throw RunUnavailable("Connection is not authenticated");
    auto profile=context_->profiles.admit_local(revision);auto captured=std::shared_ptr<CapturedLocalConnection>(new CapturedLocalConnection);captured->context_=context_;
    captured->identity_={"local",context_->profiles.local_server_identity(),context_->generation,revision,1};return captured;
}
ConnectionBindingIdentity CapturedLocalConnection::identity()const{auto life=context_->enter();return identity_;}
BoundConnectionCommands CapturedLocalConnection::commands()const{auto life=context_->enter();if(!context_->executor)throw RunUnavailable("Workspace commands require an opened workspace");return {identity_,read_local_connection_commands(*context_->executor)};}
BoundConnectionRun CapturedLocalConnection::invoke_command(ConnectionCommandRequest request)const{auto life=context_->enter();auto profile=context_->profiles.admit_local(identity_.profile_revision);auto admission=context_->admit();if(!context_->executor)throw RunUnavailable("Workspace commands require an opened workspace");return {identity_,submit_local_connection_command(context_->store,*context_->executor,context_->graphs,std::move(request))};}
BoundConnectionWorkspace CapturedLocalConnection::workspace()const{auto life=context_->enter();return {identity_,context_->executor?context_->executor->execution_workspace():ExecutionWorkspaceMetadata{}};}
BoundConnectionMembership CapturedLocalConnection::membership()const{auto life=context_->enter();if(!context_->executor)throw RunUnavailable("Workspace membership requires an opened root");const auto before=context_->executor->execution_workspace();const auto snapshot=context_->executor->workspace_membership_snapshot();validate_workspace_admission({before.workspace_id,before.authority_id},context_->executor->execution_workspace());if(snapshot.workspace_id!=before.workspace_id)throw Conflict("Workspace membership identity changed");return {identity_,snapshot};}
BoundConnectionFiles CapturedLocalConnection::files(std::string folder,std::int64_t revision,std::string path)const{auto life=context_->enter();return context_->folder_read(folder,revision,[&](const auto& snapshot,auto& tools){return BoundConnectionFiles{{identity_,snapshot.workspace_id,folder,path,revision},tools.browse_files(path)};});}
BoundConnectionFile CapturedLocalConnection::file(std::string folder,std::int64_t revision,std::string path)const{auto life=context_->enter();return context_->folder_read(folder,revision,[&](const auto& snapshot,auto& tools){return BoundConnectionFile{{identity_,snapshot.workspace_id,folder,path,revision},tools.browse_file(path)};});}
BoundConnectionFind CapturedLocalConnection::find(std::string folder,std::int64_t revision,ConnectionFindRequest request)const{auto life=context_->enter();if(request.query.empty()||request.query.size()>4096||!request.limit||request.limit>1000||(request.type!="file"&&request.type!="directory"))throw std::invalid_argument("Invalid workspace filename search");return context_->folder_read(folder,revision,[&](const auto& snapshot,auto& tools){return BoundConnectionFind{{identity_,snapshot.workspace_id,folder,request.path,revision},tools.find_files(request.query,request.path,request.limit,{},request.hidden,request.respect_ignore,request.type)};});}
BoundConnectionProviders CapturedLocalConnection::save_provider(std::string id,std::string route,std::string model,SecretBytes key,std::int64_t revision,bool activate)const{auto life=context_->enter();auto profile=context_->profiles.admit_local(identity_.profile_revision);auto admission=context_->admit();if(revision<0||revision>=9007199254740991LL)throw std::invalid_argument("Invalid provider profile revision");auto* setup=dynamic_cast<ProviderProfileSetup*>(context_->executor);if(!setup)throw RunUnavailable("Provider profiles are unavailable");return {identity_,setup->save_profile(std::move(id),std::move(route),std::move(model),std::move(key),revision,activate),setup->profile_routes()};}
BoundConnectionProviderModels CapturedLocalConnection::discover_provider_models(std::string id,std::string route,SecretBytes key,std::int64_t revision)const{auto life=context_->enter();auto profile=context_->profiles.admit_local(identity_.profile_revision);auto admission=context_->admit();if(revision<0||revision>9007199254740991LL)throw std::invalid_argument("Invalid provider profile revision");auto* setup=dynamic_cast<ProviderProfileSetup*>(context_->executor);if(!setup)throw RunUnavailable("Provider profiles are unavailable");return {identity_,setup->discover_models(std::move(id),std::move(route),std::move(key),revision)};}
BoundConnectionProviders CapturedLocalConnection::import_providers(std::string path,std::int64_t revision)const{auto life=context_->enter();auto profile=context_->profiles.admit_local(identity_.profile_revision);auto admission=context_->admit();if(path.empty()||path.size()>32768||path.find('\0')!=std::string::npos||revision<0||revision>=9007199254740991LL)throw std::invalid_argument("Invalid provider import request");auto* setup=dynamic_cast<ProviderProfileSetup*>(context_->executor);if(!setup)throw RunUnavailable("Provider profiles are unavailable");return {identity_,setup->import_yaml_configuration(std::filesystem::u8path(path),revision),setup->profile_routes()};}
BoundConnectionProviders CapturedLocalConnection::providers()const{auto life=context_->enter();auto* setup=dynamic_cast<ProviderProfileSetup*>(context_->executor);if(!setup)throw RunUnavailable("Provider profiles are unavailable");return {identity_,setup->configuration(),setup->profile_routes()};}
BoundConnectionProviders CapturedLocalConnection::select_provider(std::string id,std::int64_t revision)const{auto life=context_->enter();auto profile=context_->profiles.admit_local(identity_.profile_revision);auto admission=context_->admit();if(id.empty()||id.size()>256||id.find('\0')!=std::string::npos||revision<0||revision>=9007199254740991LL)throw std::invalid_argument("Invalid provider profile selection");auto* setup=dynamic_cast<ProviderProfileSetup*>(context_->executor);if(!setup)throw RunUnavailable("Provider profiles are unavailable");return {identity_,setup->select_profile(std::move(id),revision),setup->profile_routes()};}
BoundConnectionModels CapturedLocalConnection::models()const{auto life=context_->enter();return {identity_,context_->executor?context_->executor->models():std::vector<std::string>{}};}
namespace {
void context_model(const std::string& model){if(model.size()>256||model.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid context model");}
}
ContextManualStatus request_local_connection_context(RunExecutor& executor,const std::string& session,ConnectionContextRequest request){
    context_model(request.model);if(request.id.empty()||request.id.size()>128||request.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos||request.head<0||request.head>9007199254740991LL)throw std::invalid_argument("Invalid context request identity or revision");
    if(!executor.supports_context())throw RunUnavailable("This provider has no registered context controls");
    if(executor.supports_profile_admission()&&!request.provider)throw Conflict("Capture the active provider profile before requesting compaction");
    if(request.provider&&!executor.supports_profile_admission())throw std::invalid_argument("Context provider profile admission is unavailable");
    if(request.provider&&(request.provider->id.empty()||request.provider->id.size()>256||request.provider->id.find('\0')!=std::string::npos||request.provider->revision<0||request.provider->revision>9007199254740991LL))throw std::invalid_argument("Invalid context provider binding");
    return request.provider?executor.request_context_profile(session,request.id,"local-owner",request.head,request.model,*request.provider):executor.request_context(session,request.id,"local-owner",request.head,request.model);
}
BoundConnectionContext CapturedLocalConnection::context(const std::string& session,const std::string& model)const{auto life=context_->enter();context_model(model);(void)context_->store.session(session).get();if(!context_->executor)throw RunUnavailable("Context controls are unavailable");return {identity_,context_->executor->context_status(session,model)};}
BoundConnectionContextRequest CapturedLocalConnection::context_request(const std::string& session,const std::string& id,const std::string& model)const{auto life=context_->enter();context_model(model);if(id.empty()||id.size()>128||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::invalid_argument("Invalid context request identity");(void)context_->store.session(session).get();if(!context_->executor)throw RunUnavailable("Context controls are unavailable");return {identity_,session,context_->executor->context_request(session,id,model)};}
BoundConnectionContextRequest CapturedLocalConnection::compact_context(const std::string& session,ConnectionContextRequest request)const{auto life=context_->enter();auto profile=context_->profiles.admit_local(identity_.profile_revision);auto admission=context_->admit();(void)context_->store.session(session).get();if(!context_->executor)throw RunUnavailable("Context controls are unavailable");return {identity_,session,request_local_connection_context(*context_->executor,session,std::move(request))};}
BoundConnectionSkills CapturedLocalConnection::skills()const{auto life=context_->enter();if(!context_->executor||!context_->executor->supports_skill_catalogue())throw RunUnavailable("Workspace skill inspection is unavailable");return {identity_,context_->executor->workspace_skills()};}
BoundConnectionSessionSkills CapturedLocalConnection::session_skills(const std::string& session)const{auto life=context_->enter();(void)context_->store.session(session).get();if(!context_->executor||!context_->executor->supports_session_skills())throw RunUnavailable("Session skill controls are unavailable");return {identity_,session,context_->executor->session_skills(session)};}
BoundConnectionSessionSkills CapturedLocalConnection::select_session_skills(const std::string& session,ConnectionSkillSelectionRequest request)const{auto life=context_->enter();auto profile=context_->profiles.admit_local(identity_.profile_revision);auto admission=context_->admit();(void)context_->store.session(session).get();if(!context_->executor||!context_->executor->supports_session_skills())throw RunUnavailable("Session skill controls are unavailable");if(request.revision<0||request.revision>=9007199254740991LL)throw std::invalid_argument("Invalid session skill revision");validate_skill_selections({request.workspace.workspace_id,request.ids,{}});validate_workspace_admission(request.workspace,context_->executor->execution_workspace());return {identity_,session,context_->executor->replace_session_skills(session,std::move(request.ids),request.revision,std::move(request.workspace))};}
BoundConnectionAgents CapturedLocalConnection::agents()const{
    auto life=context_->enter();if(!context_->executor||!context_->executor->supports_agent_selection())throw RunUnavailable("Named agent selection is unavailable");
    return {identity_,context_->executor->agent_definitions()};
}
BoundConnectionAgentDefinition CapturedLocalConnection::agent_definition(const std::string& id)const{
    auto life=context_->enter();if(id.empty()||id.size()>64||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)throw std::invalid_argument("Invalid named agent ID");
    if(!context_->executor||!context_->executor->supports_agent_selection())throw RunUnavailable("Named agent definitions are unavailable");return {identity_,context_->executor->agent_definition(id)};
}
BoundConnectionSessionAgent CapturedLocalConnection::session_agent(const std::string& session)const{
    auto life=context_->enter();if(!context_->executor||!context_->executor->supports_agent_selection())throw RunUnavailable("Conversation agent selection is unavailable");return {identity_,session,context_->executor->session_agent(session)};
}
BoundConnectionSessionAgent CapturedLocalConnection::select_session_agent(const std::string& session,ConnectionAgentSelectionRequest request)const{
    auto life=context_->enter();auto profile=context_->profiles.admit_local(identity_.profile_revision);auto admission=context_->admit();if(!context_->executor)throw RunUnavailable("Conversation agent selection is unavailable");return {identity_,session,select_local_connection_agent(*context_->executor,session,std::move(request))};
}
BoundConnectionModel CapturedLocalConnection::session_model(const std::string& session)const{auto life=context_->enter();if(!context_->executor||!context_->executor->supports_session_models())throw RunUnavailable("Conversation model selection is unavailable");return {identity_,session,context_->executor->session_model(session)};}
BoundConnectionModel CapturedLocalConnection::select_session_model(const std::string& session,ConnectionModelSelectionRequest request)const{auto life=context_->enter();auto profile=context_->profiles.admit_local(identity_.profile_revision);auto admission=context_->admit();if(!context_->executor||!context_->executor->supports_session_models())throw RunUnavailable("Conversation model selection is unavailable");if(request.revision<0||request.revision>=9007199254740991LL||request.provider.revision<0||request.provider.revision>9007199254740991LL)throw std::invalid_argument("Invalid model selection revision");return {identity_,session,context_->executor->select_session_model(session,std::move(request.model),request.revision,std::move(request.provider))};}
BoundConnectionSession CapturedLocalConnection::open_session(std::string id)const{auto life=context_->enter();return {identity_,context_->store.session(id).get()};}
BoundConnectionMessages CapturedLocalConnection::messages(std::string session,std::int64_t before,std::int64_t count)const{
    auto life=context_->enter();
    if(before<0||before>9007199254740991LL||count<1||count>1000)throw std::invalid_argument("Invalid captured message page");
    (void)context_->store.session(session).get();
    if(before)(void)context_->store.message(session,before).get();
    return {identity_,session,context_->store.history_page(session,before?std::optional<std::int64_t>(before):std::nullopt,static_cast<std::size_t>(count)).get()};
}
BoundConnectionSession CapturedLocalConnection::create_session(std::string id,std::string title)const{auto life=context_->enter();auto profile=context_->profiles.admit_local(identity_.profile_revision);auto admission=context_->admit();return {identity_,context_->store.create_session(id,title).get()};}
BoundConnectionRun CapturedLocalConnection::open_run(std::string id)const{auto life=context_->enter();return {identity_,context_->store.run(id).get()};}
BoundConnectionRunMessages CapturedLocalConnection::run_messages(std::string session,std::string run,std::int64_t before,std::int64_t count)const{
    auto life=context_->enter();
    if(before<0||before>9007199254740991LL||count<1||count>1000)throw std::invalid_argument("Invalid captured run message page");
    if(context_->store.run(run).get().session_id!=session)throw Conflict("Run history differs from its captured session");
    auto messages=context_->store.run_history(run).get();
    if(before){const auto boundary=std::find_if(messages.begin(),messages.end(),[&](const auto& message){return message.sequence==before;});if(boundary==messages.end())throw Conflict("Message cursor belongs to another captured run");messages.erase(boundary,messages.end());}
    MessagePage page;const auto retained=std::min(messages.size(),static_cast<std::size_t>(count));
    if(messages.size()>retained){messages.erase(messages.begin(),messages.end()-retained);page.next_before_sequence=messages.front().sequence;}
    page.messages=std::move(messages);return {identity_,std::move(session),std::move(run),std::move(page)};
}
BoundConnectionChildHistory CapturedLocalConnection::child_history(std::string session,std::string parent,std::string child)const{
    auto life=context_->enter();const auto owner=context_->store.run(parent).get();
    if(owner.session_id!=session)throw Conflict("Child history differs from its captured session");
    std::vector<Message> history;
    if(owner.graph_root){(void)context_->store.graph_run(parent).get();const auto run=context_->store.run(child).get();if(run.parent_id!=parent||run.session_id!=session)throw NotFound("Child does not belong to this graph");history=context_->store.run_history(child).get();}
    else history=context_->store.owned_child_history(parent,child).get();
    return {identity_,std::move(session),std::move(parent),std::move(child),std::move(history)};
}
Run submit_local_connection_run(RunExecutor& executor,ConnectionRunRequest request){
    if(!request.files.empty()&&(!request.workspace||request.workspace->revision<1))throw std::invalid_argument("File attachments require the observed workspace membership revision");
    if(request.workspace)validate_workspace_admission(*request.workspace,executor.execution_workspace());
    if(request.inbox)return executor.submit_queued(request.id,request.session,request.prompt,request.model,*request.inbox,request.workspace,request.provider,std::move(request.files));
    if(!request.files.empty())return executor.submit_with_attachments(request.id,request.session,request.prompt,request.model,std::move(request.files),request.workspace,request.provider);
    if(request.workspace)return executor.submit_workspace(request.id,request.session,request.prompt,request.model,*request.workspace,request.provider);
    if(request.provider)return executor.submit_profile(request.id,request.session,request.prompt,request.model,*request.provider);
    return executor.submit_model(request.id,request.session,request.prompt,request.model);
}
BoundConnectionRun CapturedLocalConnection::start_run(std::string id,std::string session,std::string prompt)const{return start_run(ConnectionRunRequest{std::move(id),std::move(session),std::move(prompt)});}
BoundConnectionRun CapturedLocalConnection::start_run(ConnectionRunRequest request)const{auto life=context_->enter();auto profile=context_->profiles.admit_local(identity_.profile_revision);auto admission=context_->admit();if(!context_->executor)throw RunUnavailable("Run execution unavailable");return {identity_,submit_local_connection_run(*context_->executor,std::move(request))};}
BoundConnectionRun CapturedLocalConnection::start_graph(ConnectionGraphRequest request)const{
    auto life=context_->enter();auto profile=context_->profiles.admit_local(identity_.profile_revision);auto admission=context_->admit();
    if(!context_->graphs)throw RunUnavailable("Graph execution unavailable");auto& run=request.run;auto& graphs=*context_->graphs;
    if(run.inbox||request.graph.empty()||request.graph.size()>64||request.graph.find('\0')!=std::string::npos||request.revision<1||request.revision>9007199254740991LL)throw std::invalid_argument("Invalid captured graph admission");
    if(!run.files.empty()&&(!run.workspace||run.workspace->revision<1))throw std::invalid_argument("Graph attachments require the observed workspace membership revision");
    if(run.workspace){if(!context_->executor)throw RunUnavailable("Graph workspace validation unavailable");validate_workspace_admission(*run.workspace,context_->executor->execution_workspace());}
    auto result=!run.files.empty()?graphs.submit_graph_workspace_with_attachments(run.id,run.session,request.graph,request.revision,run.prompt,run.model,*run.workspace,run.provider,std::move(run.files)):
        run.workspace?graphs.submit_graph_workspace(run.id,run.session,request.graph,request.revision,run.prompt,run.model,*run.workspace,run.provider):
        run.provider?graphs.submit_graph_profile(run.id,run.session,request.graph,request.revision,run.prompt,run.model,*run.provider):graphs.submit_graph(run.id,run.session,request.graph,request.revision,run.prompt,run.model);
    return {identity_,std::move(result)};
}
ConnectionGraphSnapshot read_local_connection_graph(PersistenceService& store,GraphExecution& graphs,const std::string& id){
    for(int attempt=0;attempt<3;++attempt){
        auto record=store.graph_run(id).get();auto context=graphs.graph_context(id);
        if(!context.enabled)return {std::move(record),std::move(context)};
        const auto fresh=store.graph_run(id).get();
        if(fresh.checkpoint_revision==record.checkpoint_revision&&fresh.run.state==record.run.state)return {std::move(record),std::move(context)};
    }
    throw Conflict("Graph context observation changed");
}
BoundConnectionGraphs CapturedLocalConnection::graphs()const{auto life=context_->enter();if(!context_->graphs)throw RunUnavailable("Graph catalog unavailable");return {identity_,context_->graphs->graphs()};}
BoundConnectionGraph CapturedLocalConnection::graph(const std::string& id)const{auto life=context_->enter();if(!context_->graphs)throw RunUnavailable("Graph observation unavailable");return {identity_,read_local_connection_graph(context_->store,*context_->graphs,id)};}
BoundConnectionGraphChildren CapturedLocalConnection::graph_children(const std::string& id)const{auto life=context_->enter();if(id.empty()||id.size()>128)throw std::invalid_argument("Invalid captured graph identity");const auto root=context_->store.graph_run(id).get();return {identity_,id,root.run.session_id,context_->store.children(id).get()};}
BoundConnectionGraphUpdate CapturedLocalConnection::graph_input(const std::string& root,const std::string& owner,const std::string& node,const std::string& input,std::int64_t revision,std::int64_t owner_revision)const{
    auto life=context_->enter();auto admission=context_->admit();if(!context_->graphs)throw RunUnavailable("Graph input unavailable");
    if(root.empty()||root.size()>128||node.empty()||node.size()>64||input.empty()||input.size()>65536||revision<1||revision>9007199254740991LL||owner.size()>128||(!owner.empty()&&(owner_revision<1||owner_revision>9007199254740991LL)))throw std::invalid_argument("Invalid captured graph human input");
    if(owner.empty())return {identity_,context_->graphs->human_input(root,node,input,"local-owner",revision)};
    if(!context_->graphs->supports_graph_tree_human_input())throw RunUnavailable("Nested graph human input is unavailable");
    return {identity_,context_->graphs->tree_human_input(root,owner,node,input,"local-owner",revision,owner_revision)};
}
BoundConnectionRun CapturedLocalConnection::resume_graph(const std::string& root,std::int64_t revision)const{
    auto life=context_->enter();auto admission=context_->admit();if(!context_->graphs)throw RunUnavailable("Graph resume unavailable");if(root.empty()||root.size()>128||revision<1||revision>9007199254740991LL)throw std::invalid_argument("Invalid captured graph resume");
    return {identity_,context_->graphs->resume_graph(root,"local-owner",revision)};
}

void CapturedLocalConnection::cancel_run(const std::string& id)const{auto life=context_->enter();auto admission=context_->admit();if(!context_->executor)throw RunUnavailable("Run execution unavailable");context_->executor->cancel(id);}
namespace {
void operation_scope(LocalConnectionContext& context,const std::string& session,const std::string& run){if(context.store.run(run).get().session_id!=session)throw Conflict("Operation scope differs from its captured session");}
Operation scoped_operation(LocalConnectionContext& context,const std::string& session,const std::string& run,const std::string& id){operation_scope(context,session,run);auto value=context.store.operation(id).get();if(value.spec.run_id!=run)throw Conflict("Operation belongs to another captured run");return value;}
}
std::shared_ptr<CapturedSessionPermissions> CapturedLocalConnection::permissions(std::string session)const{auto life=context_->enter();(void)context_->store.session(session).get();auto result=std::shared_ptr<CapturedSessionPermissions>(new CapturedSessionPermissions);result->context_=context_;result->identity_=identity_;result->session_=std::move(session);return result;}
BoundConnectionPermissionReply CapturedSessionPermissions::reply(std::string id,OperationDecision decision,bool reusable)const{auto life=context_->enter();auto admission=context_->admit();if(reusable){auto profile=context_->profiles.admit_local(identity_.profile_revision);return {identity_,session_,AgentPermissionGrantStore(context_->store).reply(session_,id,decision,true)};}return {identity_,session_,AgentPermissionGrantStore(context_->store).reply(session_,id,decision,false)};}
BoundConnectionPermissions CapturedSessionPermissions::pending()const{auto life=context_->enter();(void)context_->store.session(session_).get();return {identity_,session_,AgentPermissionGrantStore(context_->store).pending(session_)};}
BoundConnectionPermissionGrants CapturedSessionPermissions::grants()const{auto life=context_->enter();(void)context_->store.session(session_).get();return {identity_,session_,AgentPermissionGrantStore(context_->store).list(session_)};}
BoundConnectionPermissionGrants CapturedSessionPermissions::revoke(std::string id,std::int64_t revision)const{auto life=context_->enter();auto profile=context_->profiles.admit_local(identity_.profile_revision);auto admission=context_->admit();(void)context_->store.session(session_).get();return {identity_,session_,AgentPermissionGrantStore(context_->store).revoke(session_,id,revision)};}
std::shared_ptr<CapturedRunOperations> CapturedLocalConnection::operations(std::string session,std::string run)const{auto life=context_->enter();operation_scope(*context_,session,run);auto result=std::shared_ptr<CapturedRunOperations>(new CapturedRunOperations);result->context_=context_;result->identity_=identity_;result->session_=std::move(session);result->run_=std::move(run);return result;}
BoundConnectionOperations CapturedRunOperations::list()const{auto life=context_->enter();operation_scope(*context_,session_,run_);return {identity_,session_,run_,context_->store.operations(run_).get()};}
BoundConnectionOperation CapturedRunOperations::open(const std::string& id)const{auto life=context_->enter();return {identity_,scoped_operation(*context_,session_,run_,id)};}
BoundConnectionOperation CapturedRunOperations::decide(const std::string& id,OperationDecision decision)const{auto life=context_->enter();auto admission=context_->admit();if(decision!=OperationDecision::allow&&decision!=OperationDecision::deny)throw std::invalid_argument("Invalid operation decision");(void)scoped_operation(*context_,session_,run_,id);return {identity_,context_->store.decide_operation(id,decision,"local-owner").get()};}
std::shared_ptr<ConnectionEventStream> CapturedLocalConnection::events(std::string session,std::string root)const{
    auto life=context_->enter();const auto run=context_->store.run(root).get();if(run.session_id!=session||!run.parent_id.empty())throw Conflict("Event stream requires its captured root session");
    auto stream=std::shared_ptr<ConnectionEventStream>(new ConnectionEventStream);stream->context_=context_;stream->identity_=identity_;stream->session_=std::move(session);stream->root_=std::move(root);return stream;
}
std::shared_ptr<ConnectionEventStream> CapturedLocalConnection::run_events(std::string session,std::string id)const{
    auto life=context_->enter();if(context_->store.run(id).get().session_id!=session)throw Conflict("Event stream differs from its captured run session");
    auto stream=std::shared_ptr<ConnectionEventStream>(new ConnectionEventStream);stream->context_=context_;stream->identity_=identity_;stream->session_=std::move(session);stream->root_=std::move(id);stream->tree_=false;return stream;
}
ConnectionEventCursor ConnectionEventStream::cursor()const{auto life=context_->enter();return {identity_.server_id,identity_.connection_id,session_,root_,0};}
ConnectionEventPage ConnectionEventStream::read(ConnectionEventCursor input)const{
    auto life=context_->enter();const ConnectionEventCursor expected{identity_.server_id,identity_.connection_id,session_,root_,input.sequence};
    if(input.sequence<0||input.sequence>9007199254740991LL||input!=expected)throw Conflict("Event cursor differs from captured connection/session/run");
    if(input.sequence){const auto boundary=tree_?context_->store.tree_events(root_,input.sequence-1,1).get():context_->store.event_batch(root_,input.sequence-1,1).get();if(boundary.empty()||boundary.front().sequence!=input.sequence)throw Conflict("Cursor sequence does not belong to the captured stream");}
    ConnectionEventPage page{tree_?context_->store.tree_events(root_,input.sequence,256).get():context_->store.event_batch(root_,input.sequence,256).get(),input};if(!page.events.empty())page.cursor.sequence=page.events.back().sequence;return page;
}
}
