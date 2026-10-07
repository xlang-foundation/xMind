#include "agentflow/agent_runner.hpp"
#include "nlohmann/json.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace agentflow {
namespace {
using Json=nlohmann::json;
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
    if(data.contains("tool_call_id")) result.tool_call_id=data["tool_call_id"].get<std::string>();
    if(data.contains("tool_calls")) for(const auto& call:data["tool_calls"]) result.tool_calls.push_back({call.at("id").get<std::string>(),call.at("name").get<std::string>(),call.at("arguments").get<std::string>()});
    return result;
}
Json assistant(const ModelCompletion& result) {
    Json value={{"content",result.content}};
    if(!result.refusal.empty()) value["refusal"]=result.refusal;
    if(!result.tool_calls.empty()) {
        value["tool_calls"]=Json::array();
        for(const auto& call:result.tool_calls) value["tool_calls"].push_back({{"id",call.id},{"name",call.name},{"arguments",call.arguments_json}});
    }
    return value;
}
}
AgentRunner::AgentRunner(PersistenceService& persistence,AgentSettings settings):persistence_(persistence),settings_(std::move(settings)) {
    if(settings_.provider.model.empty() || settings_.provider.endpoint.empty() || settings_.max_turns==0 || settings_.max_turns>128 || settings_.instructions.size()>65536 || settings_.run_timeout.count()<=0 || settings_.run_timeout.count()>3600000)
        throw std::invalid_argument("Invalid agent configuration");
    if(settings_.workspace) {
        if(settings_.provider.tools!=Capability::supported) throw std::invalid_argument("Workspace agent requires declared model tool capability");
        workspace_=std::make_unique<WorkspaceTools>(*settings_.workspace);
    }
}
AgentRunner::~AgentRunner()=default;
Run AgentRunner::start(std::string id,std::string session_id,std::string prompt) {
    if(prompt.empty() || prompt.size()>1024*1024) throw std::invalid_argument("Prompt must contain 1-1048576 UTF-8 bytes");
    return persistence_.start_prompt_run(std::move(id),std::move(session_id),Json{{"content",std::move(prompt)}}.dump()).get();
}
Run AgentRunner::execute(const std::string& id,std::stop_token token) {
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
        if(!settings_.instructions.empty()) request.messages.push_back({MessageRole::system,settings_.instructions});
        for(const auto& stored:persistence_.history(owned.session_id).get()) request.messages.push_back(message(stored));
        if(workspace_) request.tools=workspace_->definitions();
        for(std::size_t turn=0;turn<settings_.max_turns;++turn) {
            cancelled(token);
            std::optional<SecretBytes> credential;
            if(settings_.credential) credential.emplace(persistence_.resolve_credential(settings_.credential->scope,settings_.credential->id,settings_.credential->purpose).get());
            const auto response=complete_chat(settings_.provider,request,credential?&*credential:nullptr,[&](const ModelEvent& event) {
                cancelled(token);persistence_.append_event(id,event.kind,event.json).get();
            },token);
            cancelled(token);const auto reply=assistant(response);
            if(response.finish_reason=="stop") return persistence_.complete_run(id,reply.dump()).get();
            if(response.finish_reason!="tool_calls" || !workspace_) throw ModelProtocolError("Model did not produce a complete supported turn");
            std::vector<std::string> results;std::vector<ModelMessage> continuation;
            continuation.push_back({MessageRole::assistant,response.content,response.tool_calls,{},response.refusal});
            std::size_t index=0;
            for(const auto& call:response.tool_calls) {
                cancelled(token);
                const auto activity=id+":"+std::to_string(turn)+":"+std::to_string(index++);
                persistence_.append_event(id,"tool.started",Json{{"activity_id",activity},{"call_id",call.id},{"name",call.name},{"arguments",Json::parse(call.arguments_json)}}.dump()).get();
                Json output;bool success=false;
                try {output=Json::parse(workspace_->invoke(call.name,call.arguments_json,token));success=true;}
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
      catch(const ToolCancelled&) {return terminate(timed_out?RunState::failed:RunState::cancelled,{{"reason",timed_out?"agent_timeout":"cancelled"}});}
      catch(const ProviderHttpError& error) {return terminate(RunState::failed,{{"reason","provider_http_error"},{"status",error.status}});}
      catch(const TransportTimeout&) {return terminate(RunState::failed,{{"reason","provider_timeout"}});}
      catch(const TransportError&) {return terminate(RunState::failed,{{"reason","provider_transport_error"}});}
      catch(const ModelProtocolError&) {return terminate(RunState::failed,{{"reason","model_protocol_error"}});}
      catch(const std::exception&) {return terminate(RunState::failed,{{"reason","agent_error"}});}
}
}
