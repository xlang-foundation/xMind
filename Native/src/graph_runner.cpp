#include "agentflow/mcp_client.hpp"
#include "agentflow/graph_runner.hpp"
#include "agentflow/repository_instruction_context.hpp"
#include "agentflow/edit_executor.hpp"
#include "agentflow/create_executor.hpp"
#include "agentflow/patch_tool.hpp"
#include "agentflow/mcp_tool_registry.hpp"
#include "agentflow/schema_worker.hpp"
#include "agentflow/agent_authority.hpp"
#define NOMINMAX
#include <windows.h>
#include "nlohmann/json.hpp"
#include <algorithm>
#include <future>
#include <thread>
#include <random>
#include <sstream>
#include <iomanip>
#include <set>
#include <condition_variable>
#include <atomic>
namespace agentflow {
namespace {
using Json=nlohmann::json;
std::string identifier(){std::random_device random;std::ostringstream value;value<<std::hex<<std::setfill('0');for(int i=0;i<4;++i)value<<std::setw(8)<<random();return value.str();}
bool terminal(RunState state){return state==RunState::completed || state==RunState::failed || state==RunState::cancelled;}
bool configuration_id(const std::string& value){return !value.empty() && value.size()<=64 && value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")==std::string::npos;}
std::string public_identity(const AgentSettings& settings,const std::string& selected){
    const auto context=provider_context_json(settings,selected);if(!context.empty())return context;
    const auto wire=settings.provider.wire==ProviderWire::responses?"responses":settings.provider.wire==ProviderWire::anthropic_messages?"anthropic-messages":settings.provider.wire==ProviderWire::gemini_generate_content?"gemini-generate-content":"chat-completions";
    return Json{{"wire",wire},{"model_id",selected.empty()?settings.provider.model:selected}}.dump();
}
bool same_budget(const RootBudgetSpec& left,const RootBudgetSpec& right){
    return left.policy_id==right.policy_id&&left.policy_revision==right.policy_revision&&left.workspace_identity==right.workspace_identity&&
        left.provider_identity_json==right.provider_identity_json&&left.max_children==right.max_children&&left.max_parallel==right.max_parallel&&
        left.max_model_calls==right.max_model_calls&&left.wall_limit_ms==right.wall_limit_ms;
}
bool same_context(const GraphContextSpec& left,const GraphContextSpec& right){
    if(left.backend_identity!=right.backend_identity||left.agents.size()!=right.agents.size())return false;
    for(std::size_t i=0;i<left.agents.size();++i){const auto& a=left.agents[i];const auto& b=right.agents[i];
        if(a.node_id!=b.node_id||a.model_id!=b.model_id||a.provider_identity_json!=b.provider_identity_json||a.backend_identity!=b.backend_identity||a.turn_limit!=b.turn_limit)return false;
    }return true;
}
struct McpCredentialUnavailable : std::runtime_error {using std::runtime_error::runtime_error;};
}
GraphRunner::GraphRunner(PersistenceService& store,AgentSettings settings,std::size_t count):store_(store),settings_(std::move(settings)),limit_(count){
    if(count<1 || count>8 || settings_.run_timeout.count()<1 || settings_.run_timeout>std::chrono::hours(1))throw std::invalid_argument("Invalid graph execution budget");
    // MCP-only graphs do not construct AgentRunner; validate their immutable
    // trusted configuration snapshot independently of provider availability.
    if(settings_.mcp_servers.size()>16)throw std::invalid_argument("MCP server count exceeds limits");
    std::set<std::string> servers;
    for(const auto& server:settings_.mcp_servers)if(!configuration_id(server.id) || server.revision<1 || server.revision>9007199254740991LL || !servers.insert(server.id).second)throw std::invalid_argument("Invalid registered MCP graph configuration");
    if(settings_.workspace)workspace_=std::make_unique<WorkspaceTools>(*settings_.workspace);
    // Stored profiles can exist before this owner binds a workspace. Keep them
    // inactive; validate() still rejects every direct tool without a workspace.
    if(workspace_ && !settings_.process_profiles.empty()){
        process_=std::make_unique<ProcessExecutor>(store_,*workspace_,*settings_.workspace,settings_.process_profiles);
        settings_.process_profiles=process_->profiles();
    }
    if(!settings_.provider.model.empty()){
        auto graph_agent=settings_;graph_agent.delegation.reset();graph_agent.planning.reset();
        agents_=std::make_unique<AgentRunner>(store_,std::move(graph_agent));
    }
}
GraphRunner::~GraphRunner()=default;
std::string GraphRunner::provider_context(const std::string& model_id)const{return provider_context_json(settings_,model_id);}
std::vector<std::string> GraphRunner::models() const{return agents_?agents_->models():std::vector<std::string>{};}
void GraphRunner::validate(const GraphPlan& plan,const std::string& model) const{
    const auto configured=models();if(!model.empty() && std::find(configured.begin(),configured.end(),model)==configured.end())throw std::invalid_argument("Graph model is not configured");
    const std::vector<std::string> reads{"read_file","list_files","search_files","glob_files","read_repository_instructions"};
    std::size_t agent_count=0;
    for(const auto& node:plan.nodes()){
        if(node.kind==GraphNodeKind::agent){if(!agents_)throw RunUnavailable("Graph agent execution requires a configured model");if(!node.model_id.empty() && std::find(configured.begin(),configured.end(),node.model_id)==configured.end())throw std::invalid_argument("Graph node model is not configured");}
        if(node.kind==GraphNodeKind::agent)++agent_count;
        if(node.kind==GraphNodeKind::tool){
            if(!workspace_)throw std::invalid_argument("Graph tool requires a verified workspace");
            if(node.mcp){
                const auto found=std::find_if(settings_.mcp_servers.begin(),settings_.mcp_servers.end(),[&](const auto& server){return server.id==node.mcp->server_id;});
                if(found==settings_.mcp_servers.end() || !found->enabled || found->revision!=node.mcp->config_revision)throw GraphMcpUnavailable("Graph MCP server binding is unavailable or has changed");
            }else{const bool read=std::find(reads.begin(),reads.end(),node.tool)!=reads.end();const bool write=settings_.approved_edits && (node.tool=="edit_file" || node.tool=="create_file" || node.tool=="apply_patch");if(!read && !write && !(node.tool=="run_process" && process_))throw std::invalid_argument("Graph tool is not registered for direct execution");}
        }
    }
    if(settings_.context&&agent_count>8)throw GraphContextUnavailable("Context graph agent admission exceeds its shared child limit");
}
std::optional<RootBudgetSpec> GraphRunner::execution_budget(const GraphPlan& plan,const std::string& model)const{
    validate(plan,model);if(!settings_.context||std::none_of(plan.nodes().begin(),plan.nodes().end(),[](const auto& n){return n.kind==GraphNodeKind::agent;}))return {};
    return RootBudgetSpec{"native.graph-context",1,workspace_?workspace_->identity():"workspace:none",public_identity(settings_,model),8,
        static_cast<std::int64_t>(std::min<std::size_t>(limit_,2)),32,settings_.run_timeout.count()};
}
std::optional<GraphContextSpec> GraphRunner::execution_context(const GraphPlan& plan,const std::string& model)const{
    if(!execution_budget(plan,model))return {};
    auto child=settings_;child.delegation.reset();child.planning.reset();
    const auto credentials=agent_authority_credentials(store_,child);const auto workspace=workspace_?workspace_->identity():"workspace:none";
    GraphContextSpec result;auto bindings=Json::array();
    for(const auto& node:plan.nodes())if(node.kind==GraphNodeKind::agent){
        const auto selected=node.model_id.empty()?(model.empty()?settings_.provider.model:model):node.model_id;
        GraphContextAgent captured{node.id,selected,public_identity(child,selected),context_authority_identity(child,selected,workspace,credentials),static_cast<std::int64_t>(child.max_turns)};
        bindings.push_back({{"node",captured.node_id},{"model",captured.model_id},{"provider",captured.provider_identity_json},{"authority",captured.backend_identity},{"turn_limit",captured.turn_limit}});
        result.agents.push_back(std::move(captured));
    }
    result.backend_identity=context_digest("native.graph.context.v1:"+plan.json()+":"+bindings.dump());return result;
}
std::optional<GraphContextOwnerRecord> GraphRunner::captured_context(const std::string& id)const{
    try{return store_.graph_context_owner(id).get();}catch(const NotFound&){return {};}
}
void GraphRunner::validate_context_owner(const std::string& id)const{
    const auto root=store_.graph_run(id).get();const auto task=Json::parse(root.input_json);const auto model=task.value("model_id",std::string{});
    const auto saved=captured_context(id);
    if(!saved){if(execution_budget(GraphPlan(root.specification_json),model))throw GraphContextUnavailable("Graph has no captured context admission");return;}
    try{
        const GraphPlan plan(root.specification_json);const auto expected=execution_context(plan,model);const auto budget=execution_budget(plan,model);
        if(!expected||!budget||!same_context(*expected,saved->authority)||!same_budget(store_.root_budget(id).get().spec,*budget))
            throw GraphContextUnavailable("Graph context authority changed");
    }catch(const GraphMcpUnavailable&){throw GraphContextUnavailable("Graph context MCP binding is unavailable");}
     catch(const RunUnavailable&){throw GraphContextUnavailable("Graph context model execution is unavailable");}
     catch(const NotFound&){throw GraphContextUnavailable("Graph context credential metadata is unavailable");}
     catch(const std::invalid_argument&){throw GraphContextUnavailable("Graph context configuration is unavailable");}
     catch(const Conflict&){throw GraphContextUnavailable("Graph context admission is unavailable");}
}
Run GraphRunner::retire(const std::string& id,RunState state,const std::string& reason){
    if(captured_context(id))return store_.retire_graph_context_owner(id,state,reason).get();
    return store_.retire_graph_run(id,state,reason).get();
}
Run GraphRunner::tool(const std::string& id,GraphPreparedNode node,std::stop_token cancel,std::chrono::steady_clock::time_point deadline,bool shared_context){
    if(cancel.stop_requested())return store_.transition(id,RunState::queued,RunState::cancelled,R"({"reason":"cancelled_before_graph_tool"})").get();
    if(std::chrono::steady_clock::now()>=deadline)return store_.transition(id,RunState::queued,RunState::failed,R"({"reason":"graph_tool_deadline_before_start"})").get();
    store_.transition(id,RunState::queued,RunState::running).get();
    const auto& name=node.definition.tool;const auto& source=node.definition.arguments_json;
    auto started=Json{{"name",name},{"source","graph"}};
    if(node.definition.mcp)started["arguments_json"]=source;else started["arguments"]=Json::parse(source);
    store_.append_event(id,"tool.started",started.dump()).get();
    try {
        Json result;
        if(node.definition.mcp){
            const auto& binding=*node.definition.mcp;
            const auto configured=std::find_if(settings_.mcp_servers.begin(),settings_.mcp_servers.end(),[&](const auto& server){return server.id==binding.server_id && server.revision==binding.config_revision && server.enabled;});
            if(configured==settings_.mcp_servers.end())throw GraphMcpUnavailable("Graph MCP server binding is unavailable or has changed");
            const auto& server=*configured;
            const auto available=[&]{if(cancel.stop_requested())throw McpTransportCancelled("Graph MCP execution cancelled before launch");if(std::chrono::steady_clock::now()>=deadline)throw McpTransportTimeout("Graph MCP execution deadline elapsed");};
            available();
            store_.append_event(id,"mcp.connecting",Json{{"server_id",server.id},{"config_revision",server.revision},{"source","graph"}}.dump()).get();
            McpStdioConfiguration configuration{server.executable,server.working_directory,server.arguments,{}};
            struct ClearEnvironment {McpStdioConfiguration& config;~ClearEnvironment(){for(auto& entry:config.environment)if(!entry.second.empty())SecureZeroMemory(entry.second.data(),entry.second.size());}} clear{configuration};
            for(const auto& reference:server.credentials){
                available();
                try{auto secret=store_.resolve_credential(reference.scope,reference.id,mcp_credential_purpose(server,reference.name)).get();const auto bytes=secret.view();configuration.environment.emplace_back(reference.name,std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()));}
                catch(const NotFound&){throw McpCredentialUnavailable("Graph MCP credential is unavailable");}
                catch(const Conflict&){throw McpCredentialUnavailable("Graph MCP credential binding is unavailable");}
            }
            // The constructor launches the native process. Cancellation and
            // the root segment deadline are checked before acquiring it.
            available();McpStdioClient client(configuration);client.connect(deadline,cancel);
            store_.append_event(id,"mcp.connected",Json{{"server_id",server.id},{"config_revision",server.revision},{"protocol_version",client.server().protocol_version},{"source","graph"}}.dump()).get();
            McpToolRegistry registry(client,store_,*workspace_,server.id,server.revision,deadline,cancel);
            const auto definitions=registry.definitions();
            store_.append_event(id,"mcp.discovered",Json{{"server_id",server.id},{"config_revision",server.revision},{"tool_count",definitions.size()},{"source","graph"}}.dump()).get();
            if(std::none_of(definitions.begin(),definitions.end(),[&](const auto& definition){return definition.name==name;}))return store_.transition(id,RunState::running,RunState::failed,R"({"reason":"graph_mcp_alias_unavailable"})").get();
            available();
            const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
            const auto expiry=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+std::max<std::int64_t>(remaining,1);
            result=Json::parse(registry.invoke(identifier(),id,name,source,expiry,deadline,cancel));
            result["source"]="graph_tool";
            const auto output=result.dump();
            // The durable MCP operation has already succeeded. An oversized
            // graph payload is a graph failure, never a lost or replayed effect.
            if(output.size()>65536)return store_.transition(id,RunState::running,RunState::failed,Json{{"reason","graph_mcp_output_exceeds_limit"},{"operation_id",result.at("operation_id")},{"request_id",result.at("request_id")}}.dump()).get();
            store_.append_event(id,"tool.completed",Json{{"name",name},{"data",result},{"source","graph"}}.dump()).get();return store_.complete_run(id,output).get();
        }else if(name=="apply_patch"){
            // A registered direct graph step is controller-declared rather than
            // model-generated. Capture all discovered scopes before proposal;
            // later changes still retire the batch and fail this graph node.
            RepositoryInstructionContext instructions(*workspace_,workspace_->repository_instructions(".",cancel));instructions.prepare(cancel);
            PatchTool patch(store_,*workspace_,instructions);auto prepared=patch.prepare(source,cancel);
            if(!prepared){instructions.prepare(cancel);prepared=patch.prepare(source,cancel);}
            if(!prepared)throw ToolGuidanceChanged("Graph patch guidance changed during preparation");
            const auto lifetime=shared_context?std::max<std::int64_t>(1,std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count()):settings_.run_timeout.count();
            const auto expiry=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+lifetime;
            result=Json::parse(patch.execute(identifier(),id,std::move(*prepared),expiry,cancel));
            if(result.dump().size()>65536)return store_.transition(id,RunState::running,RunState::failed,Json{{"reason","graph_patch_output_exceeds_limit"},{"patch_id",result.at("patch_id")}}.dump()).get();
            if(result.at("state")!="succeeded"){
                store_.append_event(id,"tool.failed",Json{{"name",name},{"data",result},{"source","graph"}}.dump()).get();
                result["reason"]="graph_patch_incomplete";result["source"]="graph_tool";return store_.transition(id,RunState::running,RunState::failed,result.dump()).get();
            }
        }else if(name=="edit_file" || name=="create_file" || name=="run_process"){
            const auto args=Json::parse(source);const auto field=name=="run_process"?"workdir":"path";std::string directory=".";
            if(args.contains(field)){if(!args[field].is_string())throw std::invalid_argument("Invalid graph effect scope");directory=args[field].get<std::string>();}else if(name!="run_process")throw std::invalid_argument("Graph file effect requires a path");
            if(name!="run_process")directory=RepositoryInstructionContext::file_directory(directory);
            RepositoryInstructionContext instructions(*workspace_,workspace_->repository_instructions(".",cancel));instructions.ready(directory,cancel);instructions.prepare(cancel);auto guard=instructions.precondition(directory);
            const auto lifetime=shared_context?std::max<std::int64_t>(1,std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count()):settings_.run_timeout.count();
            const auto expiry=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+lifetime;
            if(name=="edit_file")result=Json::parse(EditExecutor(store_,*workspace_).invoke(identifier(),id,source,expiry,cancel,std::move(guard)));
            else if(name=="create_file")result=Json::parse(CreateExecutor(store_,*workspace_).invoke(identifier(),id,source,expiry,cancel,std::move(guard)));
            else result=Json::parse(process_->invoke(identifier(),id,source,expiry,cancel,std::move(guard)));
        }else result=Json::parse(workspace_->invoke(name,source,cancel));
        store_.append_event(id,"tool.completed",Json{{"name",name},{"data",result},{"source","graph"}}.dump()).get();result["source"]="graph_tool";return store_.complete_run(id,result.dump()).get();
    }catch(const EditOutcomeUnrecorded&){throw;}catch(const ProcessOutcomeUnrecorded&){throw;}catch(const McpOutcomeUnrecorded&){throw;}
    catch(const McpEffectUncertain&){return store_.transition(id,RunState::running,RunState::failed,R"({"reason":"graph_mcp_effect_uncertain"})").get();}
    catch(const WorkspaceEffectUncertain&){return store_.transition(id,RunState::running,RunState::failed,R"({"reason":"graph_workspace_effect_quarantined"})").get();}
    catch(const McpEffectNotDispatched&){return store_.transition(id,RunState::running,RunState::failed,R"({"reason":"graph_mcp_not_dispatched"})").get();}
    catch(const SchemaEvaluationCancelled&){return store_.transition(id,RunState::running,RunState::cancelled,R"({"reason":"graph_mcp_schema_cancelled"})").get();}
    catch(const SchemaEvaluationFailure&){return store_.transition(id,RunState::running,RunState::failed,R"({"reason":"graph_mcp_schema_evaluation_unavailable"})").get();}
    catch(const SchemaInvalid&){return store_.transition(id,RunState::running,RunState::failed,R"({"reason":"graph_mcp_schema_invalid"})").get();}
    catch(const SchemaArgumentsInvalid&){return store_.transition(id,RunState::running,RunState::failed,R"({"reason":"invalid_graph_tool_arguments"})").get();}
    catch(const McpTransportCancelled&){return store_.transition(id,RunState::running,RunState::cancelled,R"({"reason":"graph_mcp_cancelled"})").get();}
    catch(const McpTransportError&){return store_.transition(id,RunState::running,RunState::failed,R"({"reason":"graph_mcp_transport_failure"})").get();}
    catch(const McpProtocolError&){return store_.transition(id,RunState::running,RunState::failed,R"({"reason":"graph_mcp_protocol_failure"})").get();}
    catch(const McpCredentialUnavailable&){return store_.transition(id,RunState::running,RunState::failed,R"({"reason":"graph_mcp_credential_unavailable"})").get();}
    catch(const GraphMcpUnavailable&){return store_.transition(id,RunState::running,RunState::failed,R"({"reason":"graph_mcp_configuration_unavailable"})").get();}
    catch(const ToolMutationUncertain&){return store_.transition(id,RunState::running,RunState::failed,R"({"reason":"graph_file_effect_uncertain"})").get();}
    catch(const ToolGuidanceChanged&){return store_.transition(id,RunState::running,RunState::failed,R"({"reason":"graph_tool_guidance_changed"})").get();}
    catch(const ProcessEffectUncertain&){return store_.transition(id,RunState::running,RunState::failed,R"({"reason":"graph_process_effect_uncertain"})").get();}
    catch(const PermissionCancelled&){return store_.transition(id,RunState::running,RunState::cancelled,R"({"reason":"graph_tool_cancelled"})").get();}
    catch(const ToolCancelled&){return store_.transition(id,RunState::running,RunState::cancelled,R"({"reason":"graph_tool_cancelled"})").get();}
    catch(const std::invalid_argument&){return store_.transition(id,RunState::running,RunState::failed,R"({"reason":"invalid_graph_tool_arguments"})").get();}
    catch(const ToolContentConflict&){return store_.transition(id,RunState::running,RunState::failed,R"({"reason":"graph_tool_precondition_conflict"})").get();}
    catch(const ToolAccessDenied&){return store_.transition(id,RunState::running,RunState::failed,R"({"reason":"graph_tool_access_denied"})").get();}
    catch(const ToolFileError&){return store_.transition(id,RunState::running,RunState::failed,R"({"reason":"graph_tool_file_unavailable"})").get();}
    catch(const PermissionDenied&){return store_.transition(id,RunState::running,RunState::failed,R"({"reason":"graph_tool_permission_denied"})").get();}
    catch(const PermissionExpired&){return store_.transition(id,RunState::running,RunState::failed,R"({"reason":"graph_tool_permission_expired"})").get();}
    catch(const ProcessBeforeDispatchError&){return store_.transition(id,RunState::running,RunState::failed,R"({"reason":"graph_process_not_dispatched"})").get();}
}
Run GraphRunner::execute(const std::string& id,std::stop_token external,bool preserve_human_pause){
    auto root=store_.graph_run(id).get();if(root.input_json.empty())throw RunUnavailable("Legacy graph input requires review before execution");const auto input=Json::parse(root.input_json);if(!input.contains("content") || !input["content"].is_string())throw std::invalid_argument("Graph input requires task content");
    const auto model=input.value("model_id",std::string{});const GraphPlan plan(root.specification_json);validate(plan,model);
    const auto context=captured_context(id);
    if(context){
        try{validate_context_owner(id);}catch(const GraphContextUnavailable&){
            if(root.run.state==RunState::queued)return store_.retire_graph_context_owner(id,RunState::failed,R"({"reason":"graph_context_configuration_changed"})").get();
            throw;
        }
        if(root.run.state!=RunState::queued&&root.run.state!=RunState::paused)throw Conflict("Context graph is not eligible for owner activation");
        if(external.stop_requested()){
            if(root.run.state==RunState::paused&&preserve_human_pause)return root.run;
            return store_.retire_graph_context_owner(id,RunState::cancelled,R"({"reason":"graph_cancelled_before_owner_activation"})").get();
        }
    }else{
        if(execution_budget(plan,model))throw GraphContextUnavailable("Graph has no captured context admission");
        if(root.run.state==RunState::queued)root.run=store_.transition(id,RunState::queued,RunState::running).get();else if(root.run.state!=RunState::running)throw Conflict("Graph is not eligible for execution");
    }
    std::stop_source stop;std::stop_callback cancelled(external,[&]{stop.request_stop();});
    std::shared_ptr<RootExecutionBudget> budget;auto deadline=std::chrono::steady_clock::now()+settings_.run_timeout;
    if(context){
        std::optional<GraphContextOpenRecord> opened;std::chrono::steady_clock::time_point active_started;
        // Authenticated input may advance the checkpoint before a queued wake
        // claims its clock. Only typed metadata CAS races can be refreshed.
        for(std::size_t attempt=0;!opened&&attempt<128;++attempt){
            root=store_.graph_run(id).get();validate_context_owner(id);
            if(external.stop_requested()){
                if(root.run.state==RunState::paused&&preserve_human_pause)return root.run;
                return store_.retire_graph_context_owner(id,RunState::cancelled,R"({"reason":"graph_cancelled_before_owner_activation"})").get();
            }
            const auto saved=store_.root_budget(id).get();GraphContextOpenSpec activation{id,identifier(),context->authority.backend_identity,root.checkpoint_revision,saved.revision};
            active_started=std::chrono::steady_clock::now();
            try{opened=root.run.state==RunState::paused?store_.resume_graph_context_owner(activation).get():store_.open_graph_context_owner(activation).get();}
            catch(const ContextSourceChanged&){}catch(const ContextBudgetChanged&){}
        }
        if(!opened)throw GraphContextUnavailable("Graph owner metadata did not stabilize before activation");
        root=opened->graph;deadline=active_started+std::chrono::milliseconds(opened->segment.remaining_active_ms);
        try{budget=std::make_shared<RootExecutionBudget>(store_,store_.root_budget(id).get(),deadline,stop.get_token(),opened->segment,context->authority.backend_identity);}
        catch(...){throw ContextOutcomeUnrecorded("Opened graph segment has no live native clock owner; recovery is required");}
    }
    auto boundary=[&]{return GraphContextBoundarySpec{id,budget->segment_id(),context->authority.backend_identity,root.checkpoint_revision,budget->snapshot().revision,budget->active_elapsed_ms()};};
    auto retire_owner=[&](RunState state,const Json& reason){return context?store_.retire_graph_context_owner(id,state,reason.dump(),budget->segment_id(),budget->active_elapsed_ms()).get():store_.retire_graph_run(id,state,reason.dump()).get();};
    std::atomic<bool> timed_out=false;
    std::jthread timer([&](std::stop_token ending){std::mutex mutex;std::condition_variable_any changed;std::unique_lock lock(mutex);changed.wait_until(lock,ending,deadline,[]{return false;});if(!ending.stop_requested()){timed_out=true;stop.request_stop();}});
    const auto parallel=context?std::min<std::size_t>(limit_,2):limit_;
    struct Flight {std::string id;std::future<Run> result;bool slot=false;};std::vector<Flight> flights;flights.reserve(parallel);std::exception_ptr fault;bool resolution_failed=false;
    try {for(;;){
        if(std::chrono::steady_clock::now()>=deadline){timed_out=true;stop.request_stop();}
        for(auto it=flights.begin();it!=flights.end();){if(it->result.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready){++it;continue;}
            try{const auto child=it->result.get();if(!terminal(child.state))throw DatabaseError("Graph child returned without retirement");store_.settle_graph_child(child.id).get();if(child.state!=RunState::completed)stop.request_stop();}
            catch(...){if(!fault)fault=std::current_exception();stop.request_stop();}if(it->slot)budget->release_leaf();it=flights.erase(it);
        }
        if(fault){if(flights.empty())std::rethrow_exception(fault);std::this_thread::sleep_for(std::chrono::milliseconds(10));continue;}
        root=store_.graph_run(id).get();
        if(preserve_human_pause&&external.stop_requested()&&root.run.state==RunState::paused&&flights.empty())return root.run;
        if(stop.stop_requested()){
            if(!flights.empty()){std::this_thread::sleep_for(std::chrono::milliseconds(10));continue;}
            return retire_owner(external.stop_requested()?RunState::cancelled:RunState::failed,Json{{"reason",external.stop_requested()?"graph_cancelled":timed_out.load()?"graph_active_segment_timeout":resolution_failed?"graph_data_resolution_failed":"graph_child_failed"}});
        }
        GraphCoordinator coordinator(plan,root.checkpoint_json,GraphRestoreMode::live);GraphDecision decisions;
        try{decisions=coordinator.inspect();}catch(const std::invalid_argument&){resolution_failed=true;stop.request_stop();continue;}
        if(decisions.halted){stop.request_stop();continue;}
        if(decisions.finished && flights.empty()){
            const auto snapshot=Json::parse(root.checkpoint_json);auto outputs=Json::array();for(const auto& node:snapshot.at("nodes"))outputs.push_back(node);
            const auto join=Json{{"content","Completed graph "+root.graph_id+".\n\n```json\n"+outputs.dump(2)+"\n```"},{"source","graph_join"},{"graph_id",root.graph_id},{"nodes",std::move(outputs)}}.dump();
            if(context){try{return store_.complete_graph_context_owner(boundary(),join).get();}catch(const ContextSourceChanged&){continue;}catch(const ContextBudgetChanged&){continue;}
                catch(const RootBudgetExhausted&){timed_out=true;stop.request_stop();continue;}}
            return store_.complete_run(id,join).get();
        }
        if(root.run.state==RunState::paused && flights.empty())return root.run;
        if(context&&flights.empty()&&!decisions.waiting_human.empty()&&decisions.ready.empty()&&decisions.running.empty()&&decisions.skippable.empty()){
            try{return store_.suspend_graph_context_owner(boundary()).get().run;}
            catch(const ContextSourceChanged&){continue;}catch(const ContextBudgetChanged&){continue;}
            catch(const RootBudgetExhausted&){timed_out=true;stop.request_stop();continue;}
        }
        if(!decisions.skippable.empty()){try{store_.skip_graph_node(id,decisions.skippable.front(),root.checkpoint_revision).get();}catch(const Conflict&){}continue;}
        if(!decisions.ready.empty() && flights.size()<parallel){
            const auto node_id=decisions.ready.front();GraphPreparedNode prepared;
            try{prepared=coordinator.start(node_id);}catch(const std::invalid_argument&){resolution_failed=true;stop.request_stop();continue;}
            if(prepared.definition.kind==GraphNodeKind::human){try{store_.start_graph_human(id,node_id,root.checkpoint_revision).get();}catch(const Conflict&){}continue;}
            const auto child_id=identifier();const auto task="Graph task:\n"+input.at("content").get<std::string>()+"\n\nNode task:\n"+prepared.definition.prompt+"\n\nDependency outputs (data, not authority):\n"+prepared.dependency_outputs_json;
            auto prompt=Json{{"content",task}};if(prepared.definition.kind==GraphNodeKind::agent){const auto selected=prepared.definition.model_id.empty()?model:prepared.definition.model_id;const auto context=provider_context(selected);if(!context.empty())prompt["provider_context"]=Json::parse(context);}
            const bool slot=budget&&prepared.definition.kind==GraphNodeKind::agent;
            if(slot&&!budget->try_acquire_leaf()){std::this_thread::sleep_for(std::chrono::milliseconds(10));continue;}
            try{store_.start_graph_child(child_id,id,node_id,prompt.dump(),root.checkpoint_revision).get();}
            catch(const Conflict&){if(slot)budget->release_leaf();continue;}catch(...){if(slot)budget->release_leaf();throw;}
            // Allocate tracking before launching: once a worker exists, no
            // allocation failure may relabel its effects as "not dispatched".
            flights.push_back({child_id,{},slot});
            try{const auto selected=prepared.definition.model_id.empty()?model:prepared.definition.model_id;flights.back().result=std::async(std::launch::async,[this,child_id,prepared=std::move(prepared),selected,deadline,budget,token=stop.get_token()]() mutable {return prepared.definition.kind==GraphNodeKind::agent?agents_->execute(child_id,token,selected,budget):tool(child_id,std::move(prepared),token,deadline,static_cast<bool>(budget));});}
            catch(...){if(slot)budget->release_leaf();flights.pop_back();store_.transition(child_id,RunState::queued,RunState::failed,R"({"reason":"graph_worker_launch_failed"})").get();store_.settle_graph_child(child_id).get();stop.request_stop();}continue;
        }
        if(flights.empty() && decisions.waiting_human.empty())throw DatabaseError("Graph has no runnable or waiting work");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }}catch(...){
        const auto original=std::current_exception();stop.request_stop();
        // Stop and join every owned worker even when controller persistence
        // fails. Preserve the original fault and uncertain ledger for review.
        for(auto& flight:flights){try{const auto child=flight.result.get();if(terminal(child.state))store_.settle_graph_child(child.id).get();}catch(...){}if(flight.slot)budget->release_leaf();}
        std::rethrow_exception(original);
    }
}
}
