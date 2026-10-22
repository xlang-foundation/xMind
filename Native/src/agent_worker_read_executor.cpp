#include "agentflow/agent_worker_read_executor.hpp"
#include "agentflow/agent_worker_supervisor.hpp"
#include "agentflow/agent_backend_runtime.hpp"
#include "agentflow/mcp_tool_registry.hpp"
#include "agentflow/a2a_tool_registry.hpp"
#include "agentflow/language_server_tools.hpp"
#include "agentflow/create_executor.hpp"
#include "agentflow/edit_executor.hpp"
#include "agentflow/patch_tool.hpp"
#include "agentflow/restore_executor.hpp"
#include "agentflow/repository_instruction_context.hpp"
#include "agentflow/agent_authority.hpp"
#include "agentflow/delegation_executor.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <thread>

namespace agentflow {
namespace {
using Json=nlohmann::json;using Clock=std::chrono::steady_clock;using namespace std::chrono_literals;
std::string generation(){std::random_device random;std::ostringstream text;text<<std::hex<<std::setfill('0');for(unsigned i=0;i<4;++i)text<<std::setw(8)<<random();return text.str();}
bool bounded(const std::string& text,std::size_t size){return !text.empty()&&text.size()<=size&&text.find('\0')==std::string::npos;}
// Per-run native peer lifetime. No endpoint, credential or registry handle is
// carried in worker admission or remote invocation values.
class RemoteRunTools {
    PersistenceService& store_;
    std::shared_ptr<AgentBackendRuntime> runtime_;
    std::vector<std::shared_ptr<AgentMcpPeer>> peers_;
    std::map<std::string,McpToolRegistry*> mcp_;
    std::unique_ptr<A2aAgentToolRegistry> a2a_;
    std::vector<ModelToolDefinition> definitions_;
public:
    RemoteRunTools(PersistenceService& store,const AgentSettings& settings,const std::string& run,WorkspaceToolSet& members,Clock::time_point deadline,std::stop_token cancel):store_(store),runtime_(make_persistence_agent_backend_runtime(store)){
        auto& primary=members.folder_tools(0);
        for(const auto& server:settings.mcp_servers){if(!server.enabled)continue;
            store_.append_event(run,"mcp.connecting",Json{{"server_id",server.id},{"config_revision",server.revision}}.dump()).get();
            auto peer=runtime_->connect_mcp(server,deadline,cancel);peer->discover(primary,members.folders());
            const auto definitions=peer->definitions();for(const auto& definition:definitions){if(definitions_.size()>=64||!mcp_.emplace(definition.name,peer->owner_registry()).second)throw std::invalid_argument("Worker remote catalogue exceeds limits or collides");definitions_.push_back(definition);}
            store_.append_event(run,"mcp.discovered",Json{{"server_id",server.id},{"config_revision",server.revision},{"tool_count",definitions.size()},{"protocol_version",peer->protocol_version()}}.dump()).get();
            peers_.push_back(std::move(peer));
        }
        if(settings.a2a_enabled){const auto remaining=std::chrono::ceil<std::chrono::milliseconds>(deadline-Clock::now());if(remaining.count()<=0)throw AgentWorkerAuthorityExpired("Worker deadline elapsed before A2A discovery");a2a_=runtime_->discover_a2a(primary,std::min(remaining,5000ms),cancel);for(const auto& id:a2a_->unavailable_agent_ids())store_.append_event(run,"a2a.discovery_failed",Json{{"agent_id",id},{"reason","peer_unavailable_or_invalid"}}.dump()).get();for(const auto& definition:a2a_->definitions()){if(definitions_.size()>=64||mcp_.contains(definition.name))throw std::invalid_argument("Worker remote catalogue exceeds limits or collides");definitions_.push_back(definition);}}
    }
    ~RemoteRunTools(){for(const auto& peer:peers_)peer->stop_observer();}
    void observe(const std::string& run){for(const auto& peer:peers_)if(!peer->start_observer(run))store_.append_event(run,"mcp.subscription.unavailable",R"({"reason":"listener_unavailable"})").get();}
    const std::vector<ModelToolDefinition>& definitions()const noexcept{return definitions_;}
    McpToolRegistry* mcp(const std::string& alias)const{const auto found=mcp_.find(alias);return found==mcp_.end()?nullptr:found->second;}
    A2aAgentToolRegistry* a2a(const std::string& alias)const{if(!a2a_)return nullptr;const auto definitions=a2a_->definitions();return std::any_of(definitions.begin(),definitions.end(),[&](const auto& tool){return tool.name==alias;})?a2a_.get():nullptr;}
};
class WorkspaceExecutor final:public AgentRootExecutor {
    PersistenceService& store_;
    AgentSettings settings_;
    bool coding_=false,delegated_=false,graph_=false,dynamic_=false;
    std::shared_ptr<NativeChildExecutor> children_;
    std::shared_ptr<DelegationExecutor> delegation_;
    std::shared_ptr<const BoundProcessExecutable> image_;
    std::unique_ptr<WorkspaceTools> primary_;
    std::shared_ptr<ProcessExecutor> process_catalogue_;
    std::shared_ptr<LanguageServerToolRegistry> language_catalogue_;
    std::unique_ptr<AgentRunner> validator_;
    std::shared_ptr<AgentModelGateway> gateway_;
    bool callable_tool(const std::string& name)const{return std::any_of(settings_.xlang_python_tools.begin(),settings_.xlang_python_tools.end(),[&](const auto& tool){return tool.definition.name==name;});}
    std::string model(const AgentRootExecutionRequest& request)const{return request.model_id.empty()?settings_.provider.model:request.model_id;}
    std::vector<ModelToolDefinition> tools(const AgentRootExecutionRequest& request)const{
        auto selected=request.agent_tools?request.agent_tools:settings_.tool_allowlist;
        if(!primary_){if(selected&&!selected->empty())throw std::invalid_argument("Model-only worker cannot acquire tools");return {};}
        if(!selected){if(!coding_)throw std::invalid_argument("Worker read execution requires an explicit tool catalogue");selected=std::vector<std::string>{"read_file","list_files","search_files","find_files","glob_files","edit_file","create_file","apply_patch","restore_operation","read_repository_instructions","list_skills","load_skill"};if(process_catalogue_)for(const auto* name:{"run_process","wait_process","stop_process"})selected->push_back(name);if(language_catalogue_)selected->push_back("language_server");for(const auto& tool:settings_.xlang_python_tools)selected->push_back(tool.definition.name);}
        if(!delegated_&&settings_.delegation&&!request.agent_tools&&!settings_.tool_allowlist)selected->push_back("delegate_tasks");
        validate_agent_tool_allowlist(selected);std::vector<ModelToolDefinition> result;
        auto definitions=primary_->definitions();if(!delegated_&&settings_.delegation)definitions.push_back(DelegationExecutor::definition());if(coding_)for(auto definition:{EditExecutor::definition(),CreateExecutor::definition(),PatchTool::definition(),RestoreExecutor::definition()})definitions.push_back(std::move(definition));
        if(process_catalogue_){definitions.push_back(process_catalogue_->definition());definitions.push_back(process_catalogue_->wait_definition());definitions.push_back(process_catalogue_->stop_definition());}
        for(auto definition:SkillContext::definitions())definitions.push_back(std::move(definition));
        if(language_catalogue_)for(auto definition:language_catalogue_->definitions())definitions.push_back(std::move(definition));
        for(const auto& tool:settings_.xlang_python_tools){if(std::any_of(definitions.begin(),definitions.end(),[&](const auto& existing){return existing.name==tool.definition.name;}))throw std::invalid_argument("Callable alias collides with a configured native tool");definitions.push_back(tool.definition);}
        for(const auto& name:*selected){const auto found=std::find_if(definitions.begin(),definitions.end(),[&](const auto& tool){return tool.name==name;});
            if(coding_&&callable_tool(name)&&found!=definitions.end()){result.push_back(*found);continue;}
            if(coding_&&is_agent_remote_tool(name)&&(settings_.a2a_enabled||!settings_.mcp_servers.empty()))continue; // Exact discovery is checked before worker launch.
            if((!is_agent_read_tool(name)&&!is_agent_guidance_tool(name)&&!(name=="delegate_tasks"&&!delegated_&&settings_.delegation)&&!(coding_&&(is_agent_file_tool(name)||(is_agent_process_tool(name)&&process_catalogue_))))||found==definitions.end())throw std::invalid_argument("Worker read execution cannot acquire this capability");result.push_back(*found);}
        return result;
    }
public:
    WorkspaceExecutor(PersistenceService& store,AgentSettings settings,std::shared_ptr<const BoundProcessExecutable> image,bool coding,bool delegated=false,std::shared_ptr<AgentModelGateway> gateway={},bool graph=false,bool dynamic=false)
        :store_(store),settings_(std::move(settings)),coding_(coding),delegated_(delegated),graph_(graph),dynamic_(dynamic),image_(std::move(image)){
        if(!image_||(!settings_.workspace&&(coding_||delegated_||graph_||dynamic_||settings_.delegation||!settings_.language_servers.empty()||settings_.language_server_registry||settings_.language_server_dispatch))||(!coding_&&(settings_.approved_edits||settings_.a2a_enabled||!settings_.mcp_servers.empty()||!settings_.process_profiles.empty()))||((delegated_||graph_||dynamic_)&&settings_.delegation)||settings_.planning||(dynamic_&&settings_.context)||(!coding_&&settings_.workspace&&!delegated_&&!graph_&&!settings_.delegation&&settings_.context)||(!coding_&&!settings_.xlang_python_tools.empty()))throw std::invalid_argument("Unsupported worker read settings");
        if(coding_&&!settings_.approved_edits)throw std::invalid_argument("Coding worker requires approved native file tools");
        // AgentService owns the single admission-time membership capture.
        settings_.workspace_membership_snapshot={};
        if(settings_.workspace)primary_=std::make_unique<WorkspaceTools>(*settings_.workspace);
        if(!settings_.language_servers.empty()){
            language_catalogue_=settings_.language_server_registry?settings_.language_server_registry:std::make_shared<LanguageServerToolRegistry>(*primary_,settings_.language_servers);
            settings_.language_server_registry=language_catalogue_;settings_.language_servers=language_catalogue_->profiles();
        }
        if(coding_&&!settings_.process_profiles.empty()){process_catalogue_=std::make_shared<ProcessExecutor>(store_,*primary_,primary_->root_path(),settings_.process_profiles);settings_.process_profiles=process_catalogue_->profiles();}
        if(!delegated_&&settings_.delegation){if(!settings_.delegated_execution)settings_.delegated_execution=make_agent_worker_delegated_executor(store_,image_);children_=std::make_shared<NativeChildExecutor>();delegation_=std::make_shared<DelegationExecutor>(store_,children_);}
        validator_=std::make_unique<AgentRunner>(store_,settings_,delegation_);
        gateway_=gateway?std::move(gateway):make_persistence_agent_model_gateway(store_,settings_.credential);
    }
    std::shared_ptr<NativeChildExecutor> shared_children()const override{return children_;}
    std::shared_ptr<DelegationExecutor> shared_delegation()const override{return delegation_;}
    void validate(const AgentRootExecutionRequest& request)const override{
        if(!request.placement.empty()&&request.placement!="process")throw std::invalid_argument("Only process placement can enter a process worker executor");
        const auto selected=model(request);const auto models=validator_->models();
        if(!bounded(selected,256)||(!settings_.provider_identity&&std::find(models.begin(),models.end(),selected)==models.end()))throw std::invalid_argument("Worker model is not configured");
        if(request.agent_instructions.size()>32768||request.agent_instructions.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid worker agent instructions");
        validate_agent_permission_rules(request.agent_permissions);(void)tools(request);
        if(!primary_){if(request.workspace)throw std::invalid_argument("Model-only worker cannot acquire workspace membership");if(!validator_->execution_budget(selected))throw std::invalid_argument("Model-only worker requires its ordinary budget");return;}
        if(!request.workspace)throw std::invalid_argument("Worker requires captured workspace membership");
        const auto& membership=*request.workspace;
        if(membership.workspace_id!=primary_->identity()||membership.revision<1||membership.revision>9007199254740991LL||membership.folders.empty()||membership.folders.size()>256||membership.folders.front().root!=primary_->root_path()||membership.folders.front().folder_id!=membership.workspace_id)throw std::invalid_argument("Worker workspace differs from its backend owner");
        std::set<std::string> ids;std::size_t root_bytes=0;for(const auto& folder:membership.folders){if(!bounded(folder.folder_id,256)||!bounded(folder.root,32760)||folder.root.size()>262144-root_bytes||!ids.insert(folder.folder_id).second)throw std::invalid_argument("Invalid captured worker folder");root_bytes+=folder.root.size();}
        if(!validator_->execution_budget(selected))throw std::invalid_argument("Worker requires an ordinary execution budget");
    }
    Run execute(const AgentRootExecutionRequest& request,std::stop_token cancel)override{if(delegated_||graph_)throw Conflict("Child executor requires its original shared budget");return execute_owned(request,{},cancel);}
    Run execute_owned(const AgentRootExecutionRequest& request,std::shared_ptr<RootExecutionBudget> shared,std::stop_token cancel){
        if((delegated_||graph_||dynamic_)!=bool(shared))throw Conflict("Worker executor budget kind differs from captured admission");
        const auto deadline=shared?shared->deadline():Clock::now()+settings_.run_timeout;validate(request);
        const auto admitted=store_.run(request.run_id).get();
        if(admitted.graph_root||admitted.state!=RunState::queued||((graph_||dynamic_)?admitted.parent_id.empty():delegated_?admitted.parent_id!=shared->root_id():!admitted.parent_id.empty()))throw Conflict("Worker execution requires its fresh queued owner");
        if(delegated_){const auto child=store_.child_admission(request.run_id).get();if(child.kind!=ChildAdmissionKind::delegated_leaf||child.root_run_id!=shared->root_id()||child.preset_id!="workspace.inspect"||child.preset_revision!=1||settings_.max_turns>4)throw Conflict("Worker differs from bounded delegated admission");shared->check(cancel);}
        const auto history=store_.run_history(request.run_id).get();
        if(history.empty()||history.front().role!="user")throw Conflict("Worker root has no admitted prompt");
        const auto prompt=Json::parse(history.front().json);
        if(prompt.contains("agent_profile")&&prompt["agent_profile"].is_object()&&prompt["agent_profile"].contains("placement")&&prompt["agent_profile"]["placement"]!=request.placement)throw Conflict("Worker placement differs from its admitted prompt");
        const auto root=shared?shared->root_id():request.run_id;
        const auto budget=store_.root_budget(root).get();const auto expected=*validator_->execution_budget(model(request));
        auto provider_identity=Json::parse(budget.spec.provider_identity_json);if(delegated_||graph_||dynamic_)provider_identity["model_id"]=model(request);
        if(delegated_&&provider_identity!=Json::parse(provider_context_json(settings_,model(request)).empty()?expected.provider_identity_json:provider_context_json(settings_,model(request))))throw Conflict("Delegated worker provider differs from frozen settings");
        if(!delegated_&&!graph_&&!dynamic_&&(budget.spec.policy_id!=expected.policy_id||budget.spec.policy_revision!=expected.policy_revision||budget.spec.workspace_identity!=expected.workspace_identity||budget.spec.provider_identity_json!=expected.provider_identity_json||budget.spec.max_children!=expected.max_children||budget.spec.max_parallel!=expected.max_parallel||budget.spec.max_model_calls!=expected.max_model_calls||budget.spec.wall_limit_ms!=expected.wall_limit_ms||budget.model_calls_reserved!=0))throw Conflict("Worker root differs from immutable admission or was already executed");
        if(graph_){const auto child=store_.child_admission(request.run_id).get();const auto captured=store_.graph_context_owner(root).get();const auto node=std::find_if(captured.authority.agents.begin(),captured.authority.agents.end(),[&](const auto& item){return item.node_id==child.graph_node_path;});if(child.kind!=ChildAdmissionKind::graph_agent||child.root_run_id!=root||budget.spec.policy_id!="native.graph-context"||captured.authority.token_context!=settings_.context.has_value()||!captured.segment||captured.segment->state!="open"||!shared->segment_open()||shared->segment_id()!=captured.segment->id||node==captured.authority.agents.end()||node->model_id!=model(request)||node->turn_limit!=static_cast<std::int64_t>(settings_.max_turns)||node->backend_identity!=context_authority_identity(settings_,model(request),primary_->identity(),agent_authority_credentials(store_,settings_)))throw Conflict("Graph worker differs from captured scheduler authority");provider_identity=Json::parse(node->provider_identity_json);shared->check(cancel);}
        if(dynamic_){const auto child=store_.child_admission(request.run_id).get();const auto plan=store_.dynamic_plan(child.plan_id).get();const auto preset=std::find_if(plan.capabilities.presets.begin(),plan.capabilities.presets.end(),[&](const auto& p){return p.id==child.preset_id&&p.revision==child.preset_revision;});if(child.kind!=ChildAdmissionKind::dynamic_agent||child.root_run_id!=root||budget.spec.policy_id!="native.dynamic-plan"||!shared->segment_open()||shared->segment_id()!=store_.dynamic_budget_segment(root).get().id||preset==plan.capabilities.presets.end()||preset->turn_limit!=static_cast<std::int64_t>(settings_.max_turns)||preset->readonly==coding_||child.backend_identity!=agent_authority_identity(settings_,model(request),primary_->identity(),agent_authority_credentials(store_,settings_)))throw Conflict("Dynamic worker differs from signed scheduler admission");provider_identity=Json::parse(plan.capabilities.provider_identity_json);shared->check(cancel);}
        if(cancel.stop_requested())return store_.transition(request.run_id,RunState::queued,RunState::cancelled,R"({"reason":"cancelled_before_start"})").get();
        if(!primary_){
            AgentWorkerReadRequestPolicy policy;policy.instructions=settings_.instructions;if(!request.agent_instructions.empty())policy.instructions+="\n\nSelected agent instructions (trusted backend snapshot):\n"+request.agent_instructions;if(!settings_.instruction_policy.instructions.empty())policy.instruction_suffix="\n\nBackend-configured agent instructions:\n"+settings_.instruction_policy.instructions;policy.include_usage=settings_.provider.stream_usage==Capability::supported;policy.max_output_tokens=settings_.max_output_tokens;
            if(settings_.context)policy.context=AgentWorkerContextPolicy{*settings_.context,validator_->context_binding(model(request))};auto provider=settings_.provider;provider.model=model(request);if(!policy.max_output_tokens&&provider.wire==ProviderWire::anthropic_messages)policy.max_output_tokens=anthropic_default_output_tokens(provider.model);
            if(settings_.provider_identity){policy.history_owner=ProviderHistoryOwner{root,request.run_id,ModelCallRole::parent,{ContextScopeKind::session,admitted.session_id},provider_identity.dump(),context_authority_identity(settings_,provider.model,"workspace:none",agent_authority_credentials(store_,settings_))};policy.history_provider=provider;}
            AgentReadToolOwnerContext owner;owner.run_id=request.run_id;owner.deadline=deadline;return supervise(request,root,"workspace:none",1,provider_identity,deadline,std::move(owner),std::move(policy),std::move(provider),cancel);
        }
        const auto& membership=*request.workspace;WorkspaceToolSet members(membership.workspace_id,membership.revision,membership.folders);
        std::vector<std::unique_ptr<RepositoryInstructionContext>> contexts;Json folders=Json::array();
        AgentWorkerReadRequestPolicy policy;policy.instructions=settings_.instructions;
        if(!request.agent_instructions.empty())policy.instructions+="\n\nSelected agent instructions (trusted backend snapshot):\n"+request.agent_instructions;
        policy.instructions+="\n\nRegistered workspace folders for native file tools (membership captured for this run). Omit folder_id for the primary folder; specify the listed folder_id to read another folder. Treat folder names as labels, not paths.\n";
        for(std::size_t index=0;index<membership.folders.size();++index){const auto& folder=membership.folders[index];auto& files=members.folder_tools(index);
            contexts.push_back(std::make_unique<RepositoryInstructionContext>(files,files.repository_instructions(".",cancel)));
            folders.push_back({{"folder_id",folder.folder_id},{"root",folder.root}});
            auto name=std::filesystem::u8path(folder.root).filename().u8string();std::string label(reinterpret_cast<const char*>(name.data()),name.size());if(label.empty())label="workspace folder";
            if(label.size()>64){std::size_t length=64;while(length&&(static_cast<unsigned char>(label[length])&0xc0)==0x80)--length;label.resize(length);}
            policy.instructions+="- folder_id="+folder.folder_id+" name="+label+(index==0?" (primary)\n":" (attached)\n");
        }
        if(!settings_.instruction_policy.instructions.empty())policy.instruction_suffix="\n\nBackend-configured agent instructions (native permissions and execution evidence remain authoritative):\n"+settings_.instruction_policy.instructions;
        if(policy.instructions.size()+policy.instruction_suffix.size()>65536)throw std::invalid_argument("Worker instructions exceed limits");
        const auto selected_tools=tools(request);for(const auto& definition:members.definitions())if(std::any_of(selected_tools.begin(),selected_tools.end(),[&](const auto& selected){return selected.name==definition.name;}))policy.tools.push_back(definition);
        if(delegation_)for(const auto& definition:selected_tools)if(definition.name=="delegate_tasks")policy.tools.push_back(definition);
        if(coding_)for(const auto& definition:selected_tools)if(is_agent_file_tool(definition.name)||is_agent_process_tool(definition.name))policy.tools.push_back(definition);
        for(const auto& definition:selected_tools)if(definition.name=="language_server"||((definition.name=="list_skills"||definition.name=="load_skill")&&!callable_tool(definition.name)))policy.tools.push_back(definition);
        if(coding_)for(const auto& definition:selected_tools)if(callable_tool(definition.name)&&std::none_of(policy.tools.begin(),policy.tools.end(),[&](const auto& tool){return tool.name==definition.name;}))policy.tools.push_back(definition);
        std::unique_ptr<RemoteRunTools> remote_tools;
        if(coding_&&(settings_.a2a_enabled||!settings_.mcp_servers.empty())){
            remote_tools=std::make_unique<RemoteRunTools>(store_,settings_,request.run_id,members,deadline,cancel);
            const auto allowed=request.agent_tools?request.agent_tools:settings_.tool_allowlist;
            for(const auto& definition:remote_tools->definitions()){
                if(allowed&&std::find(allowed->begin(),allowed->end(),definition.name)==allowed->end())continue;
                if(policy.tools.size()>=64||std::any_of(policy.tools.begin(),policy.tools.end(),[&](const auto& tool){return tool.name==definition.name;}))throw std::invalid_argument("Worker tool catalogue exceeds limits or collides");
                policy.tools.push_back(definition);
            }
            if(allowed)for(const auto& name:*allowed)if(is_agent_remote_tool(name)&&!callable_tool(name)&&std::none_of(policy.tools.begin(),policy.tools.end(),[&](const auto& tool){return tool.name==name;}))throw std::invalid_argument("Requested worker remote tool was not discovered");
        }
        if(settings_.provider_identity){
            policy.history_owner=ProviderHistoryOwner{root,request.run_id,(delegated_||graph_||dynamic_)?ModelCallRole::leaf:ModelCallRole::parent,
                {(delegated_||graph_||dynamic_)?ContextScopeKind::execution:ContextScopeKind::session,(delegated_||graph_||dynamic_)?request.run_id:admitted.session_id},provider_identity.dump(),
                context_authority_identity(settings_,model(request),membership.workspace_id,agent_authority_credentials(store_,settings_))};
            policy.history_provider=settings_.provider;policy.history_provider->model=model(request);
        }
        if(delegation_){
            auto frozen=settings_;frozen.provider.model=model(request);frozen.tool_allowlist=std::vector<std::string>{};for(const auto& tool:policy.tools)frozen.tool_allowlist->push_back(tool.name);frozen.selectable_models=validator_->models();
            if(!request.agent_instructions.empty())frozen.instructions+="\n\nSelected agent instructions (trusted backend snapshot):\n"+request.agent_instructions;
            if(!request.agent_permissions.empty())frozen.inherited_permission_rule_layers.push_back(request.agent_permissions);
            auto shared_budget=std::make_shared<RootExecutionBudget>(store_,budget,deadline,cancel);
            policy.delegation=[executor=delegation_,frozen=std::move(frozen),shared_budget,cancel](const AgentWorkspaceInvocation& call,const std::string& assistant,std::stop_token stop){
                // Controller cancellation must reach child ownership directly;
                // an IPC handler can remain live while its caller is draining.
                std::stop_source combined;
                std::stop_callback owner_cancel(cancel,[&]{combined.request_stop();});
                std::stop_callback call_cancel(stop,[&]{combined.request_stop();});
                return AgentReadToolResult{true,executor->invoke(call.run_id,{call.call_id,call.tool_name,call.arguments_json},assistant,frozen,shared_budget,combined.get_token())};
            };
        }
        if(settings_.context)policy.context=AgentWorkerContextPolicy{*settings_.context,validator_->context_binding(model(request))};
        policy.include_usage=settings_.provider.stream_usage==Capability::supported;policy.max_output_tokens=settings_.max_output_tokens;
        auto provider=settings_.provider;provider.model=model(request);
        if(!policy.max_output_tokens&&provider.wire==ProviderWire::anthropic_messages)policy.max_output_tokens=anthropic_default_output_tokens(provider.model);
        for(const auto& event:store_.events(request.run_id).get())if(event.kind=="agent.workspace_membership")throw Conflict("Worker root already has an execution membership receipt");
        auto captured_membership=Json{{"workspace_id",membership.workspace_id},{"revision",membership.revision},{"primary_folder_id",membership.folders.front().folder_id},{"folders",std::move(folders)},{"snapshot","run_admission"}}.dump();
        if(delegated_||graph_||dynamic_){bool found=false;for(const auto& event:store_.events(root).get())if(event.kind=="agent.workspace_membership"){captured_membership=event.json;found=true;break;}if(!found)throw Conflict("Delegated worker requires the parent membership snapshot");}
        store_.append_event(request.run_id,"agent.workspace_membership",captured_membership).get();
        if(settings_.instruction_policy.revision>0)store_.append_event(request.run_id,"agent.instructions",Json{{"revision",settings_.instruction_policy.revision},{"byte_count",settings_.instruction_policy.instructions.size()},{"scope","server"},{"runtime_state","startup_snapshot"}}.dump()).get();
        AgentReadToolOwnerContext owner;owner.run_id=request.run_id;owner.primary_workspace=&members.folder_tools(0);owner.workspace_members=&members;owner.deadline=deadline;owner.approval_timeout=settings_.run_timeout;
        owner.repository_context=[&](std::size_t index){return index<contexts.size()?contexts[index].get():nullptr;};
        if(language_catalogue_)owner.language_server_query=[&](const AgentWorkspaceInvocation& call,std::stop_token stop){return language_catalogue_->invoke(call.arguments_json,stop,&members.folders(),settings_.language_server_dispatch);};
        owner.offered_tool=[catalogue=policy.tools](const std::string& name){return std::any_of(catalogue.begin(),catalogue.end(),[&](const auto& tool){return tool.name==name;});};
        owner.inherited_permission_layers=settings_.inherited_permission_rule_layers;owner.inherited_permission_layers.push_back(settings_.permission_rules);owner.permission_rules=request.agent_permissions;
        std::optional<AgentRemoteToolOwnerContext> remote_owner;
        if(remote_tools){
            AgentRemoteToolOwnerContext context;context.run_id=request.run_id;context.primary_workspace=owner.primary_workspace;context.workspace_members=&members;context.repository_context=contexts.front().get();context.offered_tool=owner.offered_tool;context.deadline=deadline;context.approval_timeout=owner.approval_timeout;
            context.mcp_registry=[&](const std::string& name){return remote_tools->mcp(name);};context.a2a_registry=[&](const std::string& name){return remote_tools->a2a(name);};
            context.refresh_catalogue=[&](std::stop_token stop){
                if(stop.stop_requested()||cancel.stop_requested())throw ToolCancelled("Worker remote discovery cancelled");
                auto next=std::make_unique<RemoteRunTools>(store_,settings_,request.run_id,members,deadline,cancel);std::vector<ModelToolDefinition> definitions;const auto allowed=request.agent_tools?request.agent_tools:settings_.tool_allowlist;
                for(const auto& tool:next->definitions())if(!allowed||std::find(allowed->begin(),allowed->end(),tool.name)!=allowed->end())definitions.push_back(tool);
                if(allowed)for(const auto& name:*allowed)if(is_agent_remote_tool(name)&&!callable_tool(name)&&std::none_of(definitions.begin(),definitions.end(),[&](const auto& tool){return tool.name==name;}))throw Conflict("Selected remote tool disappeared during discovery");
                if(stop.stop_requested()||cancel.stop_requested())throw ToolCancelled("Worker remote discovery cancelled");next->observe(request.run_id);remote_tools=std::move(next);return definitions;
            };
            remote_owner=std::move(context);remote_tools->observe(request.run_id);
        }
        std::optional<AgentCallableToolOwnerContext> callable_owner;
        if(coding_&&!settings_.xlang_python_tools.empty()){
            AgentCallableToolOwnerContext context;context.run_id=request.run_id;context.workspace_id=membership.workspace_id;context.workspace_revision=membership.revision;context.repository_context=contexts.front().get();context.offered_tool=owner.offered_tool;context.deadline=deadline;context.approval_timeout=owner.approval_timeout;
            for(const auto& tool:settings_.xlang_python_tools)if(owner.offered_tool(tool.definition.name))context.bindings.push_back({tool.definition,[binding=&tool](const std::string& arguments,std::stop_token stop){return binding->execution==XlangPythonExecution::worker?binding->callable.invoke_json_worker(arguments,stop):binding->callable.invoke_json(arguments,stop);}});
            if(!context.bindings.empty())callable_owner=std::move(context);
        }
        return supervise(request,root,membership.workspace_id,membership.revision,provider_identity,deadline,std::move(owner),std::move(policy),std::move(provider),cancel,std::move(remote_owner),std::move(callable_owner));
    }
    Run supervise(const AgentRootExecutionRequest& request,const std::string& root,const std::string& workspace_id,std::int64_t workspace_revision,const Json& provider_identity,Clock::time_point deadline,AgentReadToolOwnerContext owner,AgentWorkerReadRequestPolicy policy,ChatProviderConfig provider,std::stop_token cancel,std::optional<AgentRemoteToolOwnerContext> remote_owner={},std::optional<AgentCallableToolOwnerContext> callable_owner={}){
        auto supervisor=AgentWorkerSupervisor::prepare(store_,image_,{root,request.run_id,workspace_id,generation(),workspace_revision},provider_identity.dump(),deadline);
        // An observation failure cannot release the physical slot. Retain this
        // same supervisor, including uncertain launch before a returned handle;
        // no new child, model dispatch or durable terminal write is retried.
        const auto physical=[&]{for(;;){try{supervisor->drain_physical(Clock::now()+5s);return;}
                catch(...){std::this_thread::sleep_for(1s);}}};
        std::exception_ptr failure;
        try{supervisor->launch();if(coding_)supervisor->start_coding(owner,policy,provider,provider_context_json(settings_,provider.model),gateway_,cancel,process_catalogue_,remote_owner,callable_owner);else supervisor->start(owner,policy,provider,provider_context_json(settings_,provider.model),gateway_,cancel);(void)supervisor->execute();}
        catch(...){failure=std::current_exception();}
        try{
            if(!supervisor->admitted()){physical();if(failure)std::rethrow_exception(failure);throw Conflict("Worker lease was not admitted");}
            for(;;){try{
                    if(store_.run(request.run_id).get().state==RunState::completed)return supervisor->drain_completed(Clock::now()+5s).run;
                    const bool cancelled=cancel.stop_requested();std::string reason=cancelled?"cancelled":Clock::now()>=deadline?"deadline":"dispatch_error";
                    // Only backend model-host events may establish provider failure.
                    if(!cancelled&&reason!="deadline")for(const auto& event:store_.events(request.run_id).get()){if(event.kind!="worker.model.uncertain")continue;const auto data=Json::parse(event.json,nullptr,false);if(data.is_object()&&data.contains("generation")&&data["generation"]==supervisor->lease().generation&&data.contains("failure_code")&&data["failure_code"]=="provider_failure")reason="provider_failure";}
                    const auto observed=supervisor->drain_terminal(cancelled?RunState::cancelled:RunState::failed,reason,Clock::now()+5s).run;
                    // Preserve the backend's uncertain receipt after physical
                    // drain; the worker's generic failure cannot clear it.
                    if(supervisor->outcome_unrecorded()&&!cancelled&&Clock::now()<deadline)throw AgentWorkerReadOutcomeUnrecorded("Backend worker outcome requires ownership recovery");
                    return observed;
                }catch(const AgentWorkerChannelTimeout&){}catch(const AgentWorkerRpcTimeout&){}
            }
        }catch(...){const auto original=std::current_exception();physical();std::rethrow_exception(original);}
    }
};
}
DynamicAgentExecution make_agent_worker_dynamic_executor(PersistenceService& store,std::shared_ptr<const BoundProcessExecutable> image,std::shared_ptr<AgentModelGateway> gateway){
    if(!image)throw std::invalid_argument("Dynamic execution requires a bound xmind image");
    return [&store,image=std::move(image),gateway=std::move(gateway)](const AgentSettings& frozen,const std::string& id,std::shared_ptr<RootExecutionBudget> budget,std::stop_token cancel){
        if(!budget||!frozen.workspace||frozen.planning||frozen.context)throw Conflict("Dynamic process requires its supported captured child policy");const auto child=store.child_admission(id).get();if(child.kind!=ChildAdmissionKind::dynamic_agent||child.root_run_id!=budget->root_id())throw Conflict("Dynamic process lacks its exact child claim");const auto plan=store.dynamic_plan(child.plan_id).get();const auto preset=std::find_if(plan.capabilities.presets.begin(),plan.capabilities.presets.end(),[&](const auto& p){return p.id==child.preset_id&&p.revision==child.preset_revision;});if(preset==plan.capabilities.presets.end()||frozen.max_turns!=static_cast<std::size_t>(preset->turn_limit)||agent_authority_identity(frozen,frozen.provider.model,plan.capabilities.workspace_identity,agent_authority_credentials(store,frozen))!=child.backend_identity)throw Conflict("Dynamic process settings differ from signed preset");
        auto membership=std::make_shared<WorkspaceMembershipSnapshot>();bool found=false;for(const auto& event:store.events(budget->root_id()).get())if(event.kind=="agent.workspace_membership"){const auto value=Json::parse(event.json);membership->workspace_id=value.at("workspace_id");membership->revision=value.at("revision");for(const auto& folder:value.at("folders"))membership->folders.push_back({folder.at("folder_id").get<std::string>(),folder.at("root").get<std::string>()});found=true;break;}if(!found)throw Conflict("Dynamic process lacks captured root membership");
        WorkspaceExecutor executor(store,frozen,image,!preset->readonly,false,gateway,false,true);AgentRootExecutionRequest request;request.run_id=id;request.model_id=frozen.provider.model;request.agent_tools=preset->tools;request.workspace=std::move(membership);request.placement="process";return executor.execute_owned(request,std::move(budget),cancel);
    };
}
AgentGraphExecution make_agent_worker_graph_executor(PersistenceService& store,std::shared_ptr<const BoundProcessExecutable> image,std::shared_ptr<AgentModelGateway> gateway){
    if(!image)throw std::invalid_argument("Graph execution requires a bound xmind image");
    return [&store,image=std::move(image),gateway=std::move(gateway)](const AgentSettings& frozen,const std::string& id,std::shared_ptr<RootExecutionBudget> budget,std::string instructions,std::optional<std::vector<std::string>> tools,std::vector<AgentPermissionRule> permissions,std::stop_token cancel){
        if(!budget||!frozen.workspace)throw Conflict("Graph process requires captured workspace and context");auto settings=frozen;settings.graph_execution={};settings.delegated_execution={};
        if(!tools)tools=settings.tool_allowlist;if(!tools)tools=std::vector<std::string>{"read_file","list_files","search_files","find_files","glob_files","read_repository_instructions","list_skills","load_skill"};
        auto membership=std::make_shared<WorkspaceMembershipSnapshot>();bool found=false;for(const auto& event:store.events(budget->root_id()).get())if(event.kind=="agent.workspace_membership"){const auto value=Json::parse(event.json);membership->workspace_id=value.at("workspace_id");membership->revision=value.at("revision");for(const auto& folder:value.at("folders"))membership->folders.push_back({folder.at("folder_id").get<std::string>(),folder.at("root").get<std::string>()});found=true;break;}if(!found)throw Conflict("Graph process lacks captured root membership");
        const bool coding=settings.approved_edits;WorkspaceExecutor executor(store,std::move(settings),image,coding,false,gateway,true);AgentRootExecutionRequest request;request.run_id=id;request.model_id=frozen.provider.model;request.agent_instructions=std::move(instructions);request.agent_tools=std::move(tools);request.agent_permissions=std::move(permissions);request.workspace=std::move(membership);request.placement="process";return executor.execute_owned(request,std::move(budget),cancel);
    };
}
AgentDelegatedExecution make_agent_worker_delegated_executor(PersistenceService& store,std::shared_ptr<const BoundProcessExecutable> image,std::shared_ptr<AgentModelGateway> gateway){
    if(!image)throw std::invalid_argument("Delegated execution requires a bound xmind image");
    return [&store,image=std::move(image),gateway=std::move(gateway)](const AgentSettings& frozen,const std::string& id,std::shared_ptr<RootExecutionBudget> budget,std::vector<AgentPermissionRule> permissions,std::stop_token cancel){
        if(!budget||!frozen.workspace)throw Conflict("Delegated process requires its shared workspace owner");
        auto settings=frozen;settings.delegated_execution={};
        if(!settings.tool_allowlist){settings.tool_allowlist=std::vector<std::string>{"read_file","list_files","search_files","find_files","glob_files","read_repository_instructions","list_skills","load_skill"};if(!settings.language_servers.empty())settings.tool_allowlist->push_back("language_server");}
        std::erase_if(*settings.tool_allowlist,[](const std::string& name){return !is_agent_read_tool(name)&&!is_agent_guidance_tool(name);});
        auto membership=std::make_shared<WorkspaceMembershipSnapshot>();bool found=false;
        for(const auto& event:store.events(budget->root_id()).get())if(event.kind=="agent.workspace_membership"){const auto value=Json::parse(event.json);membership->workspace_id=value.at("workspace_id");membership->revision=value.at("revision");for(const auto& folder:value.at("folders"))membership->folders.push_back({folder.at("folder_id").get<std::string>(),folder.at("root").get<std::string>()});found=true;break;}
        if(!found)throw Conflict("Delegated process lacks captured parent membership");
        WorkspaceExecutor executor(store,std::move(settings),image,false,true,gateway);AgentRootExecutionRequest request;request.run_id=id;request.model_id=frozen.provider.model;request.workspace=std::move(membership);request.placement="process";request.agent_permissions=std::move(permissions);
        return executor.execute_owned(request,std::move(budget),cancel);
    };
}
std::shared_ptr<AgentRootExecutor> make_agent_worker_read_executor(PersistenceService& store,AgentSettings settings,std::shared_ptr<const BoundProcessExecutable> image){return std::make_shared<WorkspaceExecutor>(store,std::move(settings),std::move(image),false);}
std::shared_ptr<AgentRootExecutor> make_agent_worker_coding_executor(PersistenceService& store,AgentSettings settings,std::shared_ptr<const BoundProcessExecutable> image){return std::make_shared<WorkspaceExecutor>(store,std::move(settings),std::move(image),true);}

}
