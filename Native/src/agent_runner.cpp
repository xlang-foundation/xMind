#include "agentflow/agent_runner.hpp"
#include "nlohmann/json.hpp"
#include "agentflow/edit_executor.hpp"
#include "agentflow/mcp_tool_registry.hpp"
#include "agentflow/create_executor.hpp"
#include "agentflow/schema_worker.hpp"
#include "agentflow/repository_instruction_context.hpp"
#include "agentflow/mcp_wire.hpp"
#define NOMINMAX
#include <windows.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <map>
#include <set>

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr std::string_view instruction_prefix="\n\nBackend-configured agent instructions (native permissions and execution evidence remain authoritative):\n";
std::string operation_id() {std::random_device random;std::ostringstream value;value<<std::hex<<std::setfill('0');for(int i=0;i<4;++i) value<<std::setw(8)<<random();return value.str();}
void cancelled(std::stop_token token) {if(token.stop_requested()) throw TransportCancelled("Agent cancelled");}
MessageRole role(const std::string& name) {
    if(name=="user") return MessageRole::user;if(name=="assistant") return MessageRole::assistant;
    if(name=="tool") return MessageRole::tool;if(name=="system") return MessageRole::system;if(name=="developer") return MessageRole::developer;
    throw ModelProtocolError("Unsupported stored conversation role");
}
ModelMessage message(const Message& stored) {
    const auto data=Json::parse(stored.json);
    if(!data.is_object() || !data.contains("content") || !data["content"].is_string()) throw ModelProtocolError("Unsupported stored conversation content");
    ModelMessage result{role(stored.role),data["content"].get<std::string>()};
    if(data.contains("refusal")) result.refusal=data["refusal"].get<std::string>();
    if(data.contains("provider_items"))result.provider_items_json=data["provider_items"].dump();
    if(data.contains("tool_call_id")) result.tool_call_id=data["tool_call_id"].get<std::string>();
    if(data.contains("tool_calls")) for(const auto& call:data["tool_calls"]) result.tool_calls.push_back({call.at("id").get<std::string>(),call.at("name").get<std::string>(),call.at("arguments").get<std::string>()});
    return result;
}
Json assistant(const ModelCompletion& result,const std::string& model,std::int64_t elapsed_ms,std::optional<std::int64_t> first_token_ms) {
    Json value={{"content",result.content},{"model",model},{"elapsed_ms",elapsed_ms}};
    if(result.usage_json!="null") value["usage"]=Json::parse(result.usage_json);
    if(first_token_ms) value["first_token_ms"]=*first_token_ms;
    if(!result.refusal.empty()) value["refusal"]=result.refusal;
    if(result.provider_items_json!="[]")value["provider_items"]=Json::parse(result.provider_items_json);
    if(!result.tool_calls.empty()) {
        value["tool_calls"]=Json::array();
        for(const auto& call:result.tool_calls) value["tool_calls"].push_back({{"id",call.id},{"name",call.name},{"arguments",call.arguments_json}});
    }
    return value;
}
}
AgentRunner::AgentRunner(PersistenceService& persistence,AgentSettings settings):persistence_(persistence),settings_(std::move(settings)) {
    if(settings_.instruction_policy.instructions.size()>32768 || settings_.instruction_policy.instructions.find('\0')!=std::string::npos || settings_.instruction_policy.revision<0 || settings_.instruction_policy.revision>9007199254740991 || (!settings_.instruction_policy.instructions.empty() && settings_.instruction_policy.revision==0))throw std::invalid_argument("Invalid backend instruction policy");
    if(!settings_.instruction_policy.instructions.empty() && settings_.instructions.size()+settings_.instruction_policy.instructions.size()+instruction_prefix.size()>65536)throw std::invalid_argument("Combined agent instructions exceed limits");
    if(settings_.provider.model.empty() || settings_.provider.endpoint.empty() || settings_.max_turns==0 || settings_.max_turns>128 || settings_.instructions.size()>65536 || settings_.run_timeout.count()<=0 || settings_.run_timeout.count()>3600000)
        throw std::invalid_argument("Invalid agent configuration");
    if(settings_.workspace) {
        if(settings_.provider.tools!=Capability::supported) throw std::invalid_argument("Workspace agent requires declared model tool capability");
        workspace_=std::make_unique<WorkspaceTools>(*settings_.workspace);
    }
    if(settings_.approved_edits && !workspace_) throw std::invalid_argument("Approved edits require a workspace");
    if(!settings_.process_profiles.empty()) {
        if(!workspace_)throw std::invalid_argument("Process profiles require a verified workspace");
        process_=std::make_unique<ProcessExecutor>(persistence_,*workspace_,*settings_.workspace,settings_.process_profiles);
    }
    if(settings_.mcp_servers.size()>16)throw std::invalid_argument("MCP server count exceeds limits");
    std::set<std::string> mcp_ids;
    for(const auto& server:settings_.mcp_servers){if(server.id.empty() || server.revision<=0 || !mcp_ids.insert(server.id).second)throw std::invalid_argument("Invalid registered MCP configuration");if(server.enabled && !workspace_)throw std::invalid_argument("MCP execution requires a verified workspace and model tool capability");}
    if(settings_.selectable_models.size()>64) throw std::invalid_argument("Model catalogue exceeds limits");
    for(const auto& model:models()) if(model.empty() || model.size()>256 || model.find('\0')!=std::string::npos) throw std::invalid_argument("Invalid configured model ID");
}
AgentRunner::~AgentRunner()=default;
std::vector<std::string> AgentRunner::models() const {
    std::vector<std::string> result{settings_.provider.model};
    for(const auto& model:settings_.selectable_models) if(std::find(result.begin(),result.end(),model)==result.end()) result.push_back(model);
    return result;
}
Run AgentRunner::start(std::string id,std::string session_id,std::string prompt) {
    if(prompt.empty() || prompt.size()>1024*1024) throw std::invalid_argument("Prompt must contain 1-1048576 UTF-8 bytes");
    return persistence_.start_prompt_run(std::move(id),std::move(session_id),Json{{"content",std::move(prompt)}}.dump()).get();
}
Run AgentRunner::execute(const std::string& id,std::stop_token token,const std::string& model_id) {
    const auto admitted=persistence_.run(id).get();if(admitted.graph_root)throw std::invalid_argument("Graph roots require their owning graph executor");
    if(!admitted.parent_id.empty()){const auto graph=Json::parse(persistence_.graph_run(admitted.parent_id).get().specification_json);bool agent=false;for(const auto& node:graph.at("nodes"))if(node.at("id")==admitted.node_id && node.at("type")=="agent")agent=true;if(!agent)throw std::invalid_argument("Only agent graph children can invoke the model engine");}
    auto provider=settings_.provider;
    if(!model_id.empty()) {const auto allowed=models();if(std::find(allowed.begin(),allowed.end(),model_id)==allowed.end()) throw std::invalid_argument("Model is not configured on this backend");provider.model=model_id;}
    // Claim outside the failure handler. A duplicate worker losing this update
    // must not fail the run that another worker already owns.
    if(token.stop_requested()) return persistence_.transition(id,RunState::queued,RunState::cancelled,R"({"reason":"cancelled_before_start"})").get();
    const auto owned=persistence_.transition(id,RunState::queued,RunState::running).get();
    std::stop_source linked;
    std::stop_callback external(token,[&]{linked.request_stop();});
    std::atomic<bool> timed_out=false;
    std::jthread timer;
    token=linked.get_token();
    auto terminate=[&](RunState state,const Json& reason) {
        return persistence_.transition(id,RunState::running,state,reason.dump()).get();
    };
    try {
        timer=std::jthread([&](std::stop_token ending) {
            std::mutex mutex;std::condition_variable_any changed;std::unique_lock lock(mutex);
            changed.wait_for(lock,ending,settings_.run_timeout,[]{return false;});
            if(!ending.stop_requested()) {timed_out=true;linked.request_stop();}
        });
        ModelRequest request;request.include_usage=settings_.provider.stream_usage==Capability::supported;
        request.max_output_tokens=settings_.max_output_tokens;
        auto instructions=settings_.instructions;std::unique_ptr<RepositoryInstructionContext> repository_context;
        if(workspace_){
            const auto sources=workspace_->repository_instructions(".",token);auto metadata=Json::array();
            for(const auto& source:sources){
                metadata.push_back({{"path",source.path},{"workspace_id",source.workspace_id},{"file_id",source.file_id},{"content_sha256",source.content_sha256},{"byte_count",source.content.size()}});
            }
            persistence_.append_event(id,"agent.repository_instructions",Json{{"scope","workspace_root"},{"snapshot","run_start"},{"sources",std::move(metadata)}}.dump()).get();
            repository_context=std::make_unique<RepositoryInstructionContext>(*workspace_,sources);
            instructions+=repository_context->prepare(token);
        }
        if(!settings_.instruction_policy.instructions.empty()){instructions.append(instruction_prefix);instructions+=settings_.instruction_policy.instructions;}
        if(instructions.size()>65536)throw std::invalid_argument("Combined agent instructions exceed limits");
        if(!instructions.empty()) request.messages.push_back({MessageRole::system,std::move(instructions)});
        if(settings_.instruction_policy.revision>0)persistence_.append_event(id,"agent.instructions",Json{{"revision",settings_.instruction_policy.revision},{"byte_count",settings_.instruction_policy.instructions.size()},{"scope","server"},{"runtime_state","startup_snapshot"}}.dump()).get();
        for(const auto& stored:persistence_.run_history(id).get()) request.messages.push_back(message(stored));
        if(workspace_) request.tools=workspace_->definitions();
        if(settings_.approved_edits) request.tools.push_back(EditExecutor::definition());
        if(settings_.approved_edits) request.tools.push_back(CreateExecutor::definition());
        if(process_)request.tools.push_back(process_->definition());
        struct McpRuntime {std::unique_ptr<McpStdioClient> client;std::unique_ptr<McpToolRegistry> registry;};
        std::vector<McpRuntime> mcp_runtimes;std::map<std::string,McpToolRegistry*> mcp_tools;
        const auto run_deadline=std::chrono::steady_clock::now()+settings_.run_timeout;
        for(const auto& server:settings_.mcp_servers) {
            if(!server.enabled)continue;cancelled(token);
            persistence_.append_event(id,"mcp.connecting",Json{{"server_id",server.id},{"config_revision",server.revision}}.dump()).get();
            McpStdioConfiguration configuration{server.executable,server.working_directory,server.arguments,{}};
            struct ClearEnvironment {McpStdioConfiguration& config;~ClearEnvironment(){for(auto& entry:config.environment)if(!entry.second.empty())SecureZeroMemory(entry.second.data(),entry.second.size());}} clear{configuration};
            for(const auto& reference:server.credentials){auto secret=persistence_.resolve_credential(reference.scope,reference.id,mcp_credential_purpose(server,reference.name)).get();const auto bytes=secret.view();configuration.environment.emplace_back(reference.name,std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()));}
            McpRuntime runtime;runtime.client=std::make_unique<McpStdioClient>(configuration);runtime.client->connect(run_deadline,token);
            persistence_.append_event(id,"mcp.connected",Json{{"server_id",server.id},{"config_revision",server.revision},{"protocol_version",runtime.client->server().protocol_version}}.dump()).get();
            runtime.registry=std::make_unique<McpToolRegistry>(*runtime.client,persistence_,*workspace_,server.id,server.revision,run_deadline,token);
            const auto definitions=runtime.registry->definitions();
            for(const auto& definition:definitions){if(request.tools.size()>=64 || !mcp_tools.emplace(definition.name,runtime.registry.get()).second)throw std::invalid_argument("Model tool catalogue exceeds limits or has an alias collision");request.tools.push_back(definition);}
            persistence_.append_event(id,"mcp.discovered",Json{{"server_id",server.id},{"config_revision",server.revision},{"tool_count",definitions.size()}}.dump()).get();
            mcp_runtimes.push_back(std::move(runtime));
        }
        std::string delivered_repository_metadata;
        for(std::size_t turn=0;turn<settings_.max_turns;++turn) {
            cancelled(token);
            if(repository_context){
                auto current=settings_.instructions+repository_context->prepare(token);
                if(!settings_.instruction_policy.instructions.empty()){current.append(instruction_prefix);current+=settings_.instruction_policy.instructions;}
                if(current.size()>65536)throw ToolFileError("Combined agent instructions exceed limits");
                request.messages.front().content=std::move(current);
                const auto metadata=repository_context->metadata();if(metadata!=delivered_repository_metadata){persistence_.append_event(id,"agent.repository_scope",metadata).get();delivered_repository_metadata=metadata;}
            }
            std::optional<SecretBytes> credential;
            if(settings_.credential) credential.emplace(persistence_.resolve_credential(settings_.credential->scope,settings_.credential->id,settings_.credential->purpose).get());
            const auto response_started=std::chrono::steady_clock::now();
            std::optional<std::int64_t> first_token_ms;
            const auto response=complete_model(provider,request,credential?&*credential:nullptr,[&](const ModelEvent& event) {
                if(!first_token_ms && (event.kind=="model.text" || event.kind=="model.refusal" || event.kind=="model.tool_delta")) first_token_ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-response_started).count();
                cancelled(token);persistence_.append_event(id,event.kind,event.json).get();
            },token);
            const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-response_started).count();
            cancelled(token);const auto reply=assistant(response,provider.model,elapsed,first_token_ms);
            if(response.finish_reason=="stop") return persistence_.complete_run(id,reply.dump()).get();
            if(response.finish_reason!="tool_calls" || !workspace_) throw ModelProtocolError("Model did not produce a complete supported turn");
            std::vector<std::string> results;std::vector<ModelMessage> continuation;
            continuation.push_back({MessageRole::assistant,response.content,response.tool_calls,{},response.refusal,response.provider_items_json});
            std::size_t index=0;
            for(const auto& call:response.tool_calls) {
                cancelled(token);
                const auto activity=id+":"+std::to_string(turn)+":"+std::to_string(index++);
                persistence_.append_event(id,"tool.started",Json{{"activity_id",activity},{"call_id",call.id},{"name",call.name},{"arguments",Json::parse(call.arguments_json)}}.dump()).get();
                Json output;bool success=false;
                try {
                    bool guidance_ready=true;InstructionPrecondition guidance;
                    if(repository_context && (call.name=="read_file" || call.name=="edit_file" || call.name=="create_file" || call.name=="list_files" || call.name=="run_process")){
                        Json args;try{args=Json::parse(mcp_compact_object(call.arguments_json));}catch(const McpProtocolError&){throw std::invalid_argument("Invalid scoped tool JSON");}
                        const auto field=call.name=="run_process"?"workdir":"path";
                        std::string scope=".";
                        if(args.contains(field)){if(!args[field].is_string())throw std::invalid_argument("Invalid scoped tool path");scope=args[field].get<std::string>();}
                        else if(call.name!="run_process")throw std::invalid_argument("Scoped tool requires a path");
                        if(call.name!="run_process" && call.name!="list_files")scope=RepositoryInstructionContext::file_directory(scope);
                        guidance_ready=repository_context->ready(scope,token);
                        if(guidance_ready && (call.name=="edit_file" || call.name=="create_file" || call.name=="run_process"))guidance=repository_context->precondition(scope);
                    }
                    if(!guidance_ready){output={{"error",{{"code","repository_instructions_required"},{"message","No requested action or approval proposal occurred. Updated scoped guidance will be supplied in the next model request; reconsider this call using it."}}}};}
                    else if(call.name=="edit_file" && settings_.approved_edits) {
                        const auto expiry=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+settings_.run_timeout.count();
                        output=Json::parse(EditExecutor(persistence_,*workspace_).invoke(operation_id(),id,call.arguments_json,expiry,token,std::move(guidance)));
                    } else if(call.name=="create_file" && settings_.approved_edits) {
                        const auto expiry=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+settings_.run_timeout.count();
                        output=Json::parse(CreateExecutor(persistence_,*workspace_).invoke(operation_id(),id,call.arguments_json,expiry,token,std::move(guidance)));
                    } else if(call.name=="run_process" && process_) {
                        const auto expiry=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+settings_.run_timeout.count();
                        output=Json::parse(process_->invoke(operation_id(),id,call.arguments_json,expiry,token,std::move(guidance)));
                    } else if(const auto registered=mcp_tools.find(call.name);registered!=mcp_tools.end()) {
                        const auto expiry=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+settings_.run_timeout.count();
                        output=Json::parse(registered->second->invoke(operation_id(),id,call.name,call.arguments_json,expiry,run_deadline,token));
                    } else output=Json::parse(workspace_->invoke(call.name,call.arguments_json,token));
                    success=guidance_ready;
                }
                catch(const PermissionCancelled&) {throw;}
                catch(const PermissionDenied&) {output={{"error",{{"code","permission_denied"},{"message","Controller denied this operation; the requested effect was not dispatched"}}}};}
                catch(const PermissionExpired&) {output={{"error",{{"code","permission_expired"},{"message","Approval expired; the requested effect was not dispatched"}}}};}
                catch(const McpEffectNotDispatched&) {output={{"error",{{"code","mcp_not_dispatched"},{"message","MCP request was not dispatched"}}}};}
                catch(const ProcessBeforeDispatchError&) {output={{"error",{{"code","process_not_dispatched"},{"message","Process did not execute; launch preconditions were unavailable or changed"}}}};}
                catch(const ToolGuidanceChanged&) {output={{"error",{{"code","repository_instructions_required"},{"message","Approved proposal was retired without dispatch because repository guidance changed. Updated guidance will be supplied before a new call and new approval."}}}};}
                catch(const ToolContentConflict&) {output={{"error",{{"code","content_conflict"},{"message","File contents, identity, replacement count or target absence no longer match the operation"}}}};}
                catch(const ToolCancelled&) {throw;}
                catch(const ToolAccessDenied&) {output={{"error",{{"code","access_denied"},{"message","Workspace policy denied this operation"}}}};}
                catch(const ToolFileError&) {output={{"error",{{"code","file_unavailable"},{"message","File is unavailable, binary, outside limits or unreadable"}}}};}
                catch(const std::invalid_argument&) {output={{"error",{{"code","invalid_arguments"},{"message","Arguments do not match the tool contract"}}}};}
                cancelled(token);
                persistence_.append_event(id,success?"tool.completed":"tool.failed",Json{{"activity_id",activity},{"call_id",call.id},{"name",call.name},{"data",output}}.dump()).get();
                const auto content=output.dump();results.push_back(Json{{"content",content},{"tool_call_id",call.id}}.dump());
                continuation.push_back({MessageRole::tool,content,{},call.id});
            }
            // The assistant call and all matching results commit together; a
            // cancelled/crashed read batch cannot leave dangling call messages.
            persistence_.record_tool_turn(id,reply.dump(),std::move(results)).get();
            for(auto& next:continuation) request.messages.push_back(std::move(next));
        }
        return terminate(RunState::failed,{{"reason","model_turn_limit"}});
    } catch(const TransportCancelled&) {return terminate(timed_out?RunState::failed:RunState::cancelled,{{"reason",timed_out?"agent_timeout":"cancelled"}});}
      catch(const PermissionCancelled&) {return terminate(timed_out?RunState::failed:RunState::cancelled,{{"reason",timed_out?"agent_timeout":"cancelled"}});}
      catch(const ToolMutationUncertain&) {return terminate(RunState::failed,{{"reason","file_effect_uncertain"}});}
      catch(const EditOutcomeUnrecorded&) {throw;} // Leave claim for recovery; AgentService degrades admission.
      catch(const McpOutcomeUnrecorded&) {throw;}
      catch(const ProcessOutcomeUnrecorded&) {throw;}
      catch(const ProcessEffectUncertain&) {return terminate(RunState::failed,{{"reason","process_effect_uncertain"}});}
      catch(const McpEffectUncertain&) {return terminate(RunState::failed,{{"reason","mcp_effect_uncertain"}});}
      catch(const SchemaEvaluationCancelled&) {return terminate(timed_out?RunState::failed:RunState::cancelled,{{"reason",timed_out?"agent_timeout":"cancelled"}});}
      catch(const SchemaEvaluationFailure&) {return terminate(RunState::failed,{{"reason","mcp_schema_evaluation_unavailable"}});}
      catch(const McpTransportCancelled&) {return terminate(timed_out?RunState::failed:RunState::cancelled,{{"reason",timed_out?"agent_timeout":"cancelled"}});}
      catch(const McpTransportError&) {return terminate(RunState::failed,{{"reason","mcp_transport_failure"}});}
      catch(const McpProtocolError&) {return terminate(RunState::failed,{{"reason","mcp_protocol_failure"}});}
      catch(const ToolCancelled&) {return terminate(timed_out?RunState::failed:RunState::cancelled,{{"reason",timed_out?"agent_timeout":"cancelled"}});}
      catch(const ProviderHttpError& error) {
          Json detail={{"reason","provider_http_error"},{"status",error.status}};
          if(!error.type.empty()) detail["provider_error_type"]=error.type;
          if(!error.code.empty()) detail["provider_error_code"]=error.code;
          if(!error.param.empty()) detail["provider_error_param"]=error.param;
          return terminate(RunState::failed,detail);
      }
      catch(const TransportTimeout&) {return terminate(RunState::failed,{{"reason","provider_timeout"}});}
      catch(const TransportError&) {return terminate(RunState::failed,{{"reason","provider_transport_error"}});}
      catch(const ModelProtocolError& error) {Json detail={{"reason","model_protocol_error"}};const auto code=model_protocol_diagnostic(error);if(!code.empty())detail["protocol_error_code"]=std::string(code);return terminate(RunState::failed,detail);}
      catch(const IncompatibleProviderHistory&) {return terminate(RunState::failed,{{"reason","incompatible_provider_history"}});}
      catch(const std::exception&) {return terminate(RunState::failed,{{"reason","agent_error"}});}
}
}
