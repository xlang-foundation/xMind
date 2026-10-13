#include "agentflow/execution_platform.hpp"
#include "agentflow/skill_context.hpp"
#include "agentflow/agent_definitions.hpp"
#include "nlohmann/json.hpp"
#if defined(_WIN32)
#include "agentflow/vcs_service.hpp"
#endif
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>
#include <set>
namespace agentflow {
namespace {std::string workspace_nonce(){std::random_device random;std::ostringstream value;value<<std::hex<<std::setfill('0');for(int i=0;i<4;++i)value<<std::setw(8)<<random();return value.str();}}
struct ExecutionPlatform::LanguageServerWorkspace {
    LanguageServerWorkspace(const std::string& folder_id,std::string folder_root,const std::vector<LanguageServerProfile>& profiles)
        :root(std::move(folder_root)),workspace(root){
        if(workspace.identity()!=folder_id||workspace.root_path()!=root)throw ToolAccessDenied("Registered language-server folder identity changed");
        registry=std::make_unique<LanguageServerToolRegistry>(workspace,profiles);
    }
    std::string root;
    WorkspaceTools workspace;
    std::unique_ptr<LanguageServerToolRegistry> registry;
};
ExecutionPlatform::ExecutionPlatform(PersistenceService& store,AgentSettings settings,std::size_t workers,std::size_t capacity):store_(store) {
    workspace_membership_snapshot_=settings.workspace_membership_snapshot;
    if(settings.workspace){workspace_binding_=std::make_unique<WorkspaceTools>(*settings.workspace);settings.workspace=workspace_binding_->root_path();workspace_authority_=workspace_nonce();}
    if(workspace_binding_&&!settings.language_servers.empty()){
        language_servers_=std::make_shared<LanguageServerToolRegistry>(*workspace_binding_,settings.language_servers);
        settings.language_server_registry=language_servers_;
        settings.language_server_dispatch=[this](const std::string& server_id,const LanguageServerRequest& request,const std::vector<WorkspaceToolFolder>& folders){
            return language_server_query_admitted(request,folders,false,server_id);
        };
    }
    file_edit_proposals_=workspace_binding_&&settings.approved_edits;
    if(!settings.provider.model.empty())agents_=std::make_unique<AgentService>(store,settings,workers,capacity);
    graphs_=std::make_unique<GraphService>(store,std::move(settings),std::min<std::size_t>(workers,8),capacity);
}
ExecutionPlatform::~ExecutionPlatform()=default;
Run ExecutionPlatform::submit(std::string id,std::string session,std::string prompt){return submit_model(std::move(id),std::move(session),std::move(prompt),{});}
Run ExecutionPlatform::submit_model(std::string id,std::string session,std::string prompt,std::string model){
    if(!agents_)throw RunUnavailable("Configure a provider before running an agent");
    if(!healthy())throw RunUnavailable("Backend execution requires fault reconciliation");
    return agents_->submit_model(std::move(id),std::move(session),std::move(prompt),std::move(model));
}
Run ExecutionPlatform::submit_message(std::string id,std::string context,std::string message,std::string content,std::string identity){if(const auto replay=store_.incoming_message(message,context,identity,content).get())return *replay;if(!agents_||!healthy())throw RunUnavailable("Incoming agent execution is unavailable");return agents_->submit_message(std::move(id),std::move(context),std::move(message),std::move(content),std::move(identity));}
Run ExecutionPlatform::continue_a2a_task(std::string task,std::string message,std::string context,std::string identity,std::string content,std::string payload,std::string input){if(const auto replay=store_.a2a_continuation(message,task,context,identity,content,payload).get())return *replay;if(!agents_||!healthy())throw RunUnavailable("A2A task continuation is unavailable");return agents_->continue_a2a_task(std::move(task),std::move(message),std::move(context),std::move(identity),std::move(content),std::move(payload),std::move(input));}
ExecutionWorkspaceMetadata ExecutionPlatform::execution_workspace()const{
    if(!workspace_binding_)return {};return {true,workspace_binding_->root_path(),workspace_binding_->identity(),workspace_authority_};
}
std::unique_ptr<WorkspaceToolSet> ExecutionPlatform::workspace_membership()const{
    if(!workspace_binding_)throw RunUnavailable("Version control requires an opened workspace");
    auto snapshot=workspace_membership_snapshot_?workspace_membership_snapshot_():WorkspaceMembershipSnapshot{
        workspace_binding_->identity(),1,{{workspace_binding_->identity(),workspace_binding_->root_path()}}};
    if(snapshot.workspace_id!=workspace_binding_->identity()||snapshot.revision<1||snapshot.folders.empty()||
       snapshot.folders.front().folder_id!=workspace_binding_->identity()||snapshot.folders.front().root!=workspace_binding_->root_path())
        throw ToolAccessDenied("Current workspace membership does not match the opened primary folder");
    return std::make_unique<WorkspaceToolSet>(std::move(snapshot.workspace_id),snapshot.revision,std::move(snapshot.folders));
}
WorkspaceTools& ExecutionPlatform::vcs_folder(WorkspaceToolSet& membership,const std::string& folder_id)const{
    const auto index=folder_id.empty()?std::size_t{0}:membership.folder_index(folder_id);
    return membership.folder_tools(index);
}
void ExecutionPlatform::initialize_vcs(std::string_view provider,std::string folder_id){
#if defined(_WIN32)
    if(!workspace_binding_)throw RunUnavailable("Version control initialization requires an opened workspace");
    const auto workspace=execution_workspace();
    try{auto membership=workspace_membership();initialize_workspace_vcs(vcs_folder(*membership,folder_id),provider);validate_workspace_admission({workspace.workspace_id,workspace.authority_id},execution_workspace());}
    catch(const VcsInitializationFailure&){throw;}
#else
    (void)provider;throw RunUnavailable("Version control initialization is unavailable on this platform");
#endif
}
std::vector<WorkspaceVcsFileStatus> ExecutionPlatform::vcs_status(std::string folder_id)const{
#if defined(_WIN32)
    if(!workspace_binding_)throw RunUnavailable("Version control status requires an opened workspace");
    const auto workspace=execution_workspace();
    try{auto membership=workspace_membership();const auto values=workspace_vcs_status(vcs_folder(*membership,folder_id));validate_workspace_admission({workspace.workspace_id,workspace.authority_id},execution_workspace());
        std::vector<WorkspaceVcsFileStatus> result;result.reserve(values.size());for(const auto& value:values)result.push_back({value.file,value.additions,value.deletions,value.status});return result;}
    catch(const VcsStatusFailure&){throw;}
#else
    throw RunUnavailable("Version control status is unavailable on this platform");
#endif
}
WorkspaceVcsInfo ExecutionPlatform::vcs_info(std::string folder_id)const{
#if defined(_WIN32)
    if(!workspace_binding_)throw RunUnavailable("Version control metadata requires an opened workspace");
    const auto workspace=execution_workspace();
    try{auto membership=workspace_membership();const auto value=workspace_vcs_info(vcs_folder(*membership,folder_id));validate_workspace_admission({workspace.workspace_id,workspace.authority_id},execution_workspace());
        return {value.current_branch,value.default_branch,value.local_branches,value.repository};}
    catch(const VcsStatusFailure&){throw;}
#else
    throw RunUnavailable("Version control metadata is unavailable on this platform");
#endif
}
std::optional<WorkspaceVcsReviewBase> ExecutionPlatform::vcs_base(std::string folder_id)const{
#if defined(_WIN32)
    if(!workspace_binding_)throw RunUnavailable("VCS review base requires an opened workspace");
    const auto workspace=execution_workspace();
    try{auto membership=workspace_membership();const auto value=workspace_vcs_base(vcs_folder(*membership,folder_id));validate_workspace_admission({workspace.workspace_id,workspace.authority_id},execution_workspace());
        if(!value)return {};return WorkspaceVcsReviewBase{value->name,value->ref,value->source};}
    catch(const VcsStatusFailure&){throw;}
#else
    throw RunUnavailable("VCS review base is unavailable on this platform");
#endif
}
std::vector<WorkspaceVcsFileDiff> ExecutionPlatform::vcs_diff(std::string_view mode,std::optional<std::string> base,unsigned context,std::string folder_id)const{
#if defined(_WIN32)
    if(!workspace_binding_)throw RunUnavailable("VCS diff requires an opened workspace");
    const auto workspace=execution_workspace();
    try{auto membership=workspace_membership();const auto values=workspace_vcs_diff(vcs_folder(*membership,folder_id),mode,std::move(base),context);validate_workspace_admission({workspace.workspace_id,workspace.authority_id},execution_workspace());
        std::vector<WorkspaceVcsFileDiff> result;result.reserve(values.size());for(const auto& value:values)result.push_back({value.file,value.patch,value.status,value.additions,value.deletions});return result;}
    catch(const VcsStatusFailure&){throw;}
#else
    throw RunUnavailable("VCS diff is unavailable on this platform");
#endif
}
WorkspaceListing ExecutionPlatform::browse_workspace_files(const std::string& path)const{
    if(!workspace_binding_)throw RunUnavailable("Workspace browsing requires an opened root");
    const auto workspace=execution_workspace();auto listing=workspace_binding_->browse_files(path);
    validate_workspace_admission({workspace.workspace_id,workspace.authority_id},execution_workspace());return listing;
}
WorkspaceFind ExecutionPlatform::find_workspace_files(const std::string& query,const std::string& path,std::size_t limit,bool hidden,bool respect_ignore,std::string type)const{
    if(!workspace_binding_)throw RunUnavailable("Workspace filename search requires an opened root");
    const auto workspace=execution_workspace();auto found=workspace_binding_->find_files(query,path,limit,{},hidden,respect_ignore,std::move(type));
    validate_workspace_admission({workspace.workspace_id,workspace.authority_id},execution_workspace());return found;
}
WorkspaceFile ExecutionPlatform::browse_workspace_file(const std::string& path)const{
    if(!workspace_binding_)throw RunUnavailable("Workspace file preview requires an opened root");
    const auto workspace=execution_workspace();auto file=workspace_binding_->browse_file(path);
    validate_workspace_admission({workspace.workspace_id,workspace.authority_id},execution_workspace());return file;
}
bool ExecutionPlatform::supports_skill_catalogue()const{return static_cast<bool>(workspace_binding_);}
WorkspaceSkillCatalogue ExecutionPlatform::workspace_skills()const{
    if(!workspace_binding_)throw RunUnavailable("Workspace skill inspection requires an opened root");
    const auto workspace=execution_workspace();const auto catalogue=SkillContext(*workspace_binding_).catalogue_json();
    validate_workspace_admission({workspace.workspace_id,workspace.authority_id},execution_workspace());
    return {workspace,catalogue};
}
std::vector<LanguageServerMetadata> ExecutionPlatform::language_server_profiles()const{
    std::vector<LanguageServerMetadata> result;if(!language_servers_)return result;result.reserve(language_servers_->profiles().size());
    for(const auto& profile:language_servers_->profiles())result.push_back({profile.id,profile.language_id,profile.revision,profile.extensions});return result;
}
std::string ExecutionPlatform::language_server_query(const LanguageServerRequest& request){
    if(!workspace_binding_||!language_servers_)throw RunUnavailable("Language-server queries require an opened workspace and trusted server profile");
    auto membership=workspace_membership();
    return language_server_query_admitted(request,membership->folders(),true,{});
}
std::string ExecutionPlatform::language_server_query_admitted(const LanguageServerRequest& request,const std::vector<WorkspaceToolFolder>& folders,bool prune_cache,const std::string& server_id){
    if(!workspace_binding_||!language_servers_)throw RunUnavailable("Language-server queries require an opened workspace and trusted server profile");
    if(folders.empty()||folders.front().folder_id!=workspace_binding_->identity()||folders.front().root!=workspace_binding_->root_path())
        throw ToolAccessDenied("Admitted language-server membership does not match the primary workspace folder");
    const auto requested=request.folder_id;
    std::size_t selected=0;
    if(!requested.empty()&&requested!=workspace_binding_->identity()){
        const auto found=std::find_if(folders.begin(),folders.end(),[&](const auto& folder){return folder.folder_id==requested;});
        if(found==folders.end())throw ToolAccessDenied("Language-server folder is not part of the admitted workspace membership");
        selected=static_cast<std::size_t>(std::distance(folders.begin(),found));
    }
    {
        std::lock_guard lock(language_server_workspaces_mutex_);
        if(prune_cache){std::set<std::string> current;for(const auto& member:folders)current.insert(member.folder_id);
            for(auto item=language_server_workspaces_.begin();item!=language_server_workspaces_.end();)
                if(!current.contains(item->first))item=language_server_workspaces_.erase(item);else ++item;}
    }
    if(selected==0){auto primary=request;primary.folder_id.clear();return server_id.empty()?language_servers_->query(primary):language_servers_->query(primary,server_id);}
    const auto& folder=folders[selected];std::shared_ptr<LanguageServerWorkspace> target;
    {
        std::lock_guard lock(language_server_workspaces_mutex_);
        const auto found=language_server_workspaces_.find(folder.folder_id);
        if(found!=language_server_workspaces_.end()){
            if(found->second->root!=folder.root)throw ToolAccessDenied("Registered language-server folder root changed");
            target=found->second;
        }else{
            target=std::make_shared<LanguageServerWorkspace>(folder.folder_id,folder.root,language_servers_->profiles());
            language_server_workspaces_.emplace(folder.folder_id,target);
        }
    }
    auto scoped=request;scoped.folder_id.clear();auto result=nlohmann::json::parse(server_id.empty()?target->registry->query(scoped):target->registry->query(scoped,server_id));
    result["folder_id"]=folder.folder_id;return result.dump();
}
WorkspaceSessionSkills ExecutionPlatform::session_skills(const std::string& session)const{
    if(!workspace_binding_)throw RunUnavailable("Session skills require an opened root");const auto workspace=execution_workspace();
    auto state=store_.session_skills(session,workspace.workspace_id).get();validate_workspace_admission({workspace.workspace_id,workspace.authority_id},execution_workspace());return {workspace,std::move(state)};
}
WorkspaceSessionSkills ExecutionPlatform::replace_session_skills(const std::string& session,std::vector<std::string> ids,std::int64_t revision,WorkspaceAdmission expected){
    if(!workspace_binding_||!healthy())throw RunUnavailable("Session skill controls are unavailable");const auto workspace=execution_workspace();validate_workspace_admission(expected,workspace);
    SkillSelections selections{workspace.workspace_id,ids,ids};if(!ids.empty()){SkillContext validation(*workspace_binding_);validation.restore(selections);validation.prepare();validation.precondition().verify({});}
    validate_workspace_admission(expected,execution_workspace());return {workspace,store_.replace_session_skills(session,std::move(selections),revision).get()};
}
Run ExecutionPlatform::submit_workspace(std::string id,std::string session,std::string prompt,std::string model,WorkspaceAdmission expected,std::optional<ProviderProfileAdmission> profile){
    validate_workspace_admission(expected,execution_workspace());if(profile)throw RunUnavailable("Provider profile admission is unavailable");
    return submit_model(std::move(id),std::move(session),std::move(prompt),std::move(model));
}
Run ExecutionPlatform::submit_graph_workspace(std::string id,std::string session,std::string graph,std::int64_t revision,std::string prompt,std::string model,WorkspaceAdmission expected,std::optional<ProviderProfileAdmission> profile){
    validate_workspace_admission(expected,execution_workspace());if(profile)throw RunUnavailable("Graph provider profile admission is unavailable");
    return submit_graph(std::move(id),std::move(session),std::move(graph),revision,std::move(prompt),std::move(model));
}
std::vector<std::string> ExecutionPlatform::models() const{return agents_?agents_->models():std::vector<std::string>{};}
std::vector<AgentDefinitionMetadata> ExecutionPlatform::agent_definitions()const{
    std::vector<AgentDefinitionMetadata> result;for(const auto& item:AgentDefinitionStore(store_).load().entries)result.push_back({item.id,item.revision,item.model_id});return result;
}
AgentDefinition ExecutionPlatform::agent_definition(const std::string& id)const{
    const auto catalog=AgentDefinitionStore(store_).load();const auto found=std::find_if(catalog.entries.begin(),catalog.entries.end(),[&](const auto& item){return item.id==id;});
    if(found==catalog.entries.end())throw NotFound("Named agent definition not found");return *found;
}
SessionAgentState ExecutionPlatform::session_agent(const std::string& session)const{return store_.session_agent(session).get();}
SessionAgentState ExecutionPlatform::select_session_agent(const std::string& session,std::optional<AgentDefinitionMetadata> selected,std::int64_t revision){
    std::optional<AgentDefinition> pinned;if(selected){if(selected->id.empty()||selected->revision<1)throw std::invalid_argument("Invalid named agent selection");const auto catalog=AgentDefinitionStore(store_).load();const auto found=std::find_if(catalog.entries.begin(),catalog.entries.end(),[&](const auto& item){return item.id==selected->id;});
        if(found==catalog.entries.end()||found->revision!=selected->revision||found->model_id!=selected->model_id)throw Conflict("Named agent definition changed; refresh before selecting it");pinned=*found;}
    return store_.replace_session_agent(session,std::move(pinned),revision).get();
}
bool ExecutionPlatform::supports_delegation()const{return agents_&&healthy()&&agents_->supports_delegation();}
bool ExecutionPlatform::supports_dynamic_planning()const{return agents_&&healthy()&&agents_->supports_dynamic_planning();}
bool ExecutionPlatform::supports_context()const{return agents_&&healthy()&&agents_->supports_context();}
ContextControlSnapshot ExecutionPlatform::context_status(const std::string& session,const std::string& model)const{
    if(!agents_||!healthy())throw RunUnavailable("Native context controls are unavailable");return agents_->context_status(session,model);
}
ContextManualStatus ExecutionPlatform::context_request(const std::string& session,const std::string& request,const std::string& model)const{
    if(!agents_||!healthy())throw RunUnavailable("Native context controls are unavailable");return agents_->context_request(session,request,model);
}
ContextManualStatus ExecutionPlatform::request_context(const std::string& session,const std::string& request,const std::string& actor,std::int64_t revision,const std::string& model){
    if(!agents_||!healthy())throw RunUnavailable("Native context controls are unavailable");return agents_->request_context(session,request,actor,revision,model);
}
Run ExecutionPlatform::plan_input(const std::string& root,const std::string& request,std::string input,const std::string& actor,std::int64_t revision,std::int64_t sequence){
    if(!agents_||!healthy())throw RunUnavailable("Native dynamic input is unavailable");
    return agents_->plan_input(root,request,std::move(input),actor,revision,sequence);
}
Run ExecutionPlatform::resume_plan(const std::string& root,const std::string& actor,std::int64_t revision,std::int64_t sequence){
    if(!agents_||!healthy())throw RunUnavailable("Native dynamic resume is unavailable");
    return agents_->resume_plan(root,actor,revision,sequence);
}
void ExecutionPlatform::cancel(const std::string& id){
    const auto run=store_.run(id).get();
    if(run.graph_root){graphs_->cancel(id,"local-owner");return;}
    if(!run.parent_id.empty())throw Conflict("Graph children are controlled through their owning root");
    if(!agents_)throw RunUnavailable("Provider is not configured");agents_->cancel(id);
}
bool ExecutionPlatform::healthy() const{return (!agents_ || agents_->healthy()) && graphs_->healthy();}
bool ExecutionPlatform::available() const{return static_cast<bool>(agents_);}
bool ExecutionPlatform::idle() const{return (!agents_ || agents_->idle()) && graphs_->idle();}
std::vector<GraphExecutionMetadata> ExecutionPlatform::graphs() const{auto result=graphs_->graphs();if(!healthy())for(auto& graph:result)graph.executable=false;return result;}
Run ExecutionPlatform::submit_graph(std::string id,std::string session,std::string graph,std::int64_t revision,std::string prompt,std::string model){if(!healthy())throw RunUnavailable("Backend execution requires fault reconciliation");return graphs_->submit_graph(std::move(id),std::move(session),std::move(graph),revision,std::move(prompt),std::move(model));}
GraphRootRecord ExecutionPlatform::human_input(const std::string& root,const std::string& node,const std::string& input,const std::string& actor,std::int64_t revision){return graphs_->human_input(root,node,input,actor,revision);}
Run ExecutionPlatform::resume_graph(const std::string& root,const std::string& actor,std::int64_t revision){if(!healthy())throw RunUnavailable("Native graph resume is unavailable");return graphs_->resume_graph(root,actor,revision);}
GraphContextMetadata ExecutionPlatform::graph_context(const std::string& root)const{
    auto result=graphs_->graph_context(root);if(!healthy())result.resumable=false;return result;
}
}
