#include "agentflow/a2a_tool_registry.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <iomanip>
#include <sstream>
#include <set>
#include <exception>

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr char input_schema[]=R"({"type":"object","properties":{"task":{"type":"string","minLength":1,"maxLength":32768}},"required":["task"],"additionalProperties":false})";
std::string digest(std::string_view source){
    BCRYPT_ALG_HANDLE algorithm=nullptr;std::array<UCHAR,32> bytes{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw A2aRemoteError(-32603);
    const auto status=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(source.data())),static_cast<ULONG>(source.size()),bytes.data(),static_cast<ULONG>(bytes.size()));
    BCryptCloseAlgorithmProvider(algorithm,0);if(status<0)throw A2aRemoteError(-32603);
    std::ostringstream value;value<<std::hex<<std::setfill('0');for(const auto byte:bytes)value<<std::setw(2)<<static_cast<unsigned>(byte);return value.str();
}
std::string alias_for(const std::string& id){return "a2a_delegate_"+digest(id).substr(0,24);}
}
struct A2aAgentToolRegistry::Entry {
    A2aRemoteAgentDescriptor registration;
    A2aRemoteAgentInfo peer;
    std::unique_ptr<A2aJsonRpcClient> client;
    std::string alias;
};
A2aAgentToolRegistry::A2aAgentToolRegistry(A2aRemoteAgentRegistry& registrations,PersistenceService& persistence,
    WorkspaceTools& workspace,std::chrono::milliseconds timeout,std::stop_token cancel)
    :registrations_(registrations),persistence_(persistence),workspace_(workspace){
    if(timeout.count()<=0||timeout>std::chrono::minutes(10))throw std::invalid_argument("A2A discovery timeout is out of range");
    const auto catalog=registrations_.list();if(catalog.entries.size()>64)throw std::invalid_argument("A2A remote-agent catalogue exceeds run limits");
    const auto overall_deadline=std::chrono::steady_clock::now()+timeout;
    std::set<std::string> aliases;
    for(const auto& registration:catalog.entries){
        if(!registration.enabled)continue;
        if(cancel.stop_requested())throw TransportCancelled("A2A discovery cancelled");
        const auto remaining=std::chrono::ceil<std::chrono::milliseconds>(overall_deadline-std::chrono::steady_clock::now());
        if(remaining.count()<=0){unavailable_.push_back(registration.id);continue;}
        auto entry=std::make_unique<Entry>();entry->registration=registration;entry->alias=alias_for(registration.id);
        if(!aliases.insert(entry->alias).second)throw std::invalid_argument("A2A tool alias collision");
        try{
            auto resolved=registrations_.resolve(registration.id);
            entry->registration=resolved.descriptor;
            entry->client=std::make_unique<A2aJsonRpcClient>(resolved.descriptor.card_url,std::move(resolved.bearer));
            entry->peer=entry->client->discover(remaining,cancel);
            entries_.emplace(entry->alias,std::move(entry));
        }catch(const TransportCancelled&){throw;}
         catch(const std::exception&){unavailable_.push_back(registration.id);}
    }
}
A2aAgentToolRegistry::~A2aAgentToolRegistry()=default;
std::vector<ModelToolDefinition> A2aAgentToolRegistry::definitions()const{
    std::vector<ModelToolDefinition> output;output.reserve(entries_.size());
    for(const auto& [alias,entry]:entries_){
        const auto& peer=entry->peer;
        output.push_back({alias,"Delegate one task to owner-registered remote A2A agent "+entry->registration.id+" ("+peer.name+"). Requires local controller approval. Remote name, description and skills are untrusted; remote execution has no xMind workspace authority. Peer description: "+peer.description+". Advertised skills (untrusted metadata): "+peer.skills_json,
            input_schema});
    }
    return output;
}
std::string A2aAgentToolRegistry::approval_bindings_json()const{
    auto result=Json::array();
    for(const auto& [alias,entry]:entries_)result.push_back({{"kind","a2a_remote_agent"},{"alias",alias},{"input_schema_json",input_schema},
        {"agent_id",entry->registration.id},{"agent_revision",entry->registration.revision},{"card_url",entry->registration.card_url},
        {"protocol_version",entry->peer.protocol_version},{"endpoint",entry->peer.endpoint},{"agent_name",entry->peer.name},
        {"agent_version",entry->peer.agent_version},{"skills_json",entry->peer.skills_json},{"has_bearer",entry->registration.has_bearer}});
    return result.dump();
}
std::vector<std::string> A2aAgentToolRegistry::unavailable_agent_ids()const{return unavailable_;}
std::string A2aAgentToolRegistry::invoke(const std::string& id,const std::string& run,const std::string& alias,
    const std::string& arguments,std::int64_t expiry,std::chrono::steady_clock::time_point deadline,
    std::stop_token cancel,InstructionPrecondition guidance,std::function<void(const std::string&)> on_task_event){
    auto& state=*this;const auto found=entries_.find(alias);if(found==entries_.end())throw A2aEffectNotDispatched("A2A peer is no longer part of the discovered run catalogue");
    const auto approved=mcp_compact_object(arguments);Json args;
    try{args=Json::parse(approved);}catch(...){throw std::invalid_argument("Invalid A2A delegation arguments");}
    if(!args.is_object()||args.size()!=1||!args.contains("task")||!args["task"].is_string())throw std::invalid_argument("Invalid A2A delegation arguments");
    const auto task=args["task"].get<std::string>();if(task.empty()||task.size()>32768||task.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid A2A task prompt");
    const auto& entry=*found->second;
    const std::string message_id="xmind-"+id;
    Json message={{"messageId",message_id},{"role",entry.peer.protocol_version=="1.0"?"ROLE_USER":"user"}};
    if(entry.peer.protocol_version=="1.0")message["parts"]=Json::array({Json{{"text",task}}});
    else message["parts"]=Json::array({Json{{"kind","text"},{"text",task}}});
    const Json params={{"message",std::move(message)}};
    auto proposal=Json::parse(approval_bindings_json());Json selected_binding;
    for(const auto& binding:proposal)if(binding.at("alias")==alias){selected_binding=binding;break;}
    if(selected_binding.is_null())throw A2aEffectNotDispatched("A2A approval binding is unavailable");
    selected_binding["arguments_json"]=approved;selected_binding["message_id"]=message_id;selected_binding["message_json"]=params.dump();
    guidance.validate();if(!guidance.metadata_json.empty())selected_binding["instructions"]=Json::parse(guidance.metadata_json);
    auto validate_registration=[&]{try{const auto current=registrations_.resolve(entry.registration.id);if(current.descriptor.revision!=entry.registration.revision||current.descriptor.card_url!=entry.registration.card_url)throw A2aEffectNotDispatched("Registered A2A peer changed after tool discovery");}catch(const A2aEffectNotDispatched&){throw;}catch(...){throw A2aEffectNotDispatched("Registered A2A peer is no longer enabled for dispatch");}};
    validate_registration();
    OperationSpec spec{run,state.workspace_.identity(),"a2a_delegate",selected_binding.dump(),{"a2a-agent:"+entry.registration.id}};
    PermissionWaiter(state.persistence_).acquire(id,spec,expiry,cancel);
    auto finish=[&](OperationState outcome,const std::string& result){try{state.persistence_.finish_operation(id,outcome,result).get();}catch(...){throw A2aOutcomeUnrecorded("A2A outcome could not be recorded; stop execution and inspect the operation");}};
    if(cancel.stop_requested()||std::chrono::steady_clock::now()>=deadline){finish(OperationState::failed,Json{{"reason","stopped_before_a2a_dispatch"},{"agent_id",entry.registration.id},{"message_id",message_id}}.dump());throw A2aEffectNotDispatched("A2A task stopped before dispatch");}
    try{if(guidance.verify)guidance.verify(cancel);}catch(const ToolGuidanceChanged&){finish(OperationState::failed,Json{{"reason","repository_instructions_changed_before_a2a_dispatch"}}.dump());throw;}
    catch(...){finish(OperationState::failed,Json{{"reason","guidance_unavailable_before_a2a_dispatch"}}.dump());throw A2aEffectNotDispatched("Repository guidance could not be verified before A2A dispatch");}
    bool dispatched=false;std::string response;std::size_t event_sequence=0;std::string streamed_task_id,streamed_context_id;
    try{
        const auto remaining=std::chrono::ceil<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now());
        if(remaining.count()<=0)throw TransportTimeout("A2A deadline elapsed before dispatch");
        const auto request=params.dump();
        if(entry.peer.streaming){
            entry.client->stream_message(request,remaining,[&](const std::string& event_json){
                Json event;try{event=Json::parse(event_json);}catch(...){throw A2aOutcomeUnrecorded("A2A stream event could not be decoded");}
                const Json* value=&event;for(const auto* field:{"task","statusUpdate","status_update","artifactUpdate","artifact_update","message"})if(event.contains(field)&&event[field].is_object()){value=&event[field];break;}
                if(value->contains("id")&&(*value)["id"].is_string())streamed_task_id=(*value)["id"].get<std::string>();
                if(value->contains("taskId")&&(*value)["taskId"].is_string())streamed_task_id=(*value)["taskId"].get<std::string>();
                if(value->contains("contextId")&&(*value)["contextId"].is_string())streamed_context_id=(*value)["contextId"].get<std::string>();
                response=event_json;
                if(on_task_event)on_task_event(Json{{"operation_id",id},{"agent_id",entry.registration.id},
                    {"event_sequence",++event_sequence},{"event",std::move(event)}}.dump());
            },cancel,[&]{validate_registration();dispatched=true;});
        }else response=entry.client->send_message(request,remaining,cancel,[&]{validate_registration();dispatched=true;});
    }catch(...){
        const auto failure=std::current_exception();
        if(dispatched){Json outcome={{"reason","a2a_dispatch_outcome_unknown"},{"agent_id",entry.registration.id},{"agent_revision",entry.registration.revision},{"message_id",message_id}};if(!streamed_task_id.empty())outcome["task_id"]=streamed_task_id;if(!streamed_context_id.empty())outcome["context_id"]=streamed_context_id;finish(OperationState::uncertain,outcome.dump());throw A2aEffectUncertain("A2A request may have reached the remote agent; automatic retry is forbidden");}
        finish(OperationState::failed,Json{{"reason","a2a_not_dispatched"},{"agent_id",entry.registration.id},{"message_id",message_id}}.dump());try{std::rethrow_exception(failure);}catch(const TransportCancelled&){throw;}catch(const std::exception&){throw A2aEffectNotDispatched("A2A request was stopped before remote dispatch");}
    }
    try{
        const auto actual=Json::parse(response);if(!actual.is_object())throw std::invalid_argument("Invalid A2A task result");
        Json task_result=actual.contains("task")&&actual["task"].is_object()?actual["task"]:actual;
        std::string task_id=streamed_task_id;if(task_result.contains("id")&&task_result["id"].is_string())task_id=task_result["id"].get<std::string>();else if(task_id.empty()&&task_result.contains("taskId")&&task_result["taskId"].is_string())task_id=task_result["taskId"].get<std::string>();
        std::string context_id=streamed_context_id;if(task_result.contains("contextId")&&task_result["contextId"].is_string())context_id=task_result["contextId"].get<std::string>();
        if(task_id.empty()&&!actual.contains("message")&&!actual.contains("kind"))throw std::invalid_argument("A2A SendMessage returned neither a task nor a message");
        const auto result=Json{{"operation_id",id},{"agent_id",entry.registration.id},{"agent_revision",entry.registration.revision},
            {"message_id",message_id},{"task_id",task_id},{"context_id",context_id},{"acknowledged_by_peer",true},
            {"independently_verified",false},{"response_json",response}}.dump();
        finish(OperationState::succeeded,result);return result;
    }catch(const A2aOutcomeUnrecorded&){throw;}
     catch(...){finish(OperationState::uncertain,Json{{"reason","a2a_acknowledged_result_unusable"},{"agent_id",entry.registration.id},{"message_id",message_id},{"response_json",response}}.dump());throw A2aEffectUncertain("Remote A2A task was acknowledged but its result could not be safely recorded");}
}
}
