#include "agentflow/agent_callable_tool_host.hpp"
#include "agentflow/agent_run_journal.hpp"
#include "agentflow/permission_waiter.hpp"
#include "agentflow/repository_instruction_context.hpp"
#include "agentflow/json_schema.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <iomanip>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>

namespace agentflow {
namespace {
using Json=nlohmann::json;
bool identifier(const std::string& value,std::size_t limit){return !value.empty()&&value.size()<=limit&&std::none_of(value.begin(),value.end(),[](unsigned char c){return c<0x21||c>0x7e;});}
std::string operation_id(){std::random_device random;std::ostringstream value;value<<std::hex<<std::setfill('0');for(int i=0;i<4;++i)value<<std::setw(8)<<random();return value.str();}
AgentCallableToolResult guidance_required(){return {false,R"({"error":{"code":"repository_instructions_required","message":"No trusted callable was invoked. Updated repository or skill guidance will be supplied before a new call."}})"};}
class LocalCallableToolHost final:public AgentCallableToolHost {
public:
    LocalCallableToolHost(AgentRunJournal& journal,AgentCallableToolOwnerContext owner):journal_(journal),owner_(std::move(owner)){
        if(!identifier(owner_.run_id,256)||!identifier(owner_.workspace_id,256)||owner_.workspace_revision<1||!owner_.offered_tool||owner_.bindings.empty()||owner_.bindings.size()>64||owner_.approval_timeout.count()<=0)throw std::invalid_argument("Invalid callable owner");
        for(const auto& layer:owner_.permission_layers)validate_agent_permission_rules(layer);
        for(auto& binding:owner_.bindings){
            validate_agent_tool_allowlist(std::vector<std::string>{binding.definition.name});
            const auto alias=binding.definition.name;
            if(!binding.invoke||binding.definition.input_schema_json.size()>65536||!bindings_.emplace(alias,std::move(binding)).second)throw std::invalid_argument("Invalid callable binding");
        }
    }
    AgentCallableToolResult invoke(const AgentCallableInvocation& call,std::stop_token cancel)override{
        if(call.protocol_version!=1||!identifier(call.run_id,256)||!identifier(call.attempt_id,256)||!identifier(call.call_id,256)||!identifier(call.workspace_id,256)||!identifier(call.tool_name,128)||call.arguments_json.size()>256*1024)throw std::invalid_argument("Invalid callable invocation");
        const auto check=[&]{if(cancel.stop_requested()||std::chrono::steady_clock::now()>=owner_.deadline)throw ToolCancelled("Callable owner cancelled or expired");if(journal_.run(owner_.run_id).get().state!=RunState::running)throw ToolAccessDenied("Callable owner is no longer running");};
        check();if(call.run_id!=owner_.run_id||call.workspace_id!=owner_.workspace_id||call.workspace_revision!=owner_.workspace_revision)throw ToolAccessDenied("Callable invocation differs from its admitted owner");
        const auto found=bindings_.find(call.tool_name);if(found==bindings_.end()||!owner_.offered_tool(call.tool_name))throw ToolAccessDenied("Callable is outside the admitted catalogue");
        {std::lock_guard lock(mutex_);if(calls_.size()>=128*64||!calls_.emplace(call.attempt_id,call.call_id).second)throw ToolAccessDenied("Callable identity is already used or exhausted");}
        bool asks=false;for(const auto& layer:owner_.permission_layers){const auto effect=agent_permission_effect(layer,call.tool_name,"*");if(effect==AgentPermissionEffect::deny)throw AgentPermissionDenied("Selected agent denied this callable");asks=asks||effect==AgentPermissionEffect::ask;}
        JsonSchema schema(found->second.definition.input_schema_json);schema.validate_object(call.arguments_json);const auto arguments=Json::parse(call.arguments_json);
        const auto ready=[&]{return !owner_.repository_context||owner_.repository_context->ready(".",cancel);};
        if(!ready())return guidance_required();
        const auto guidance=owner_.repository_context?owner_.repository_context->precondition("."):InstructionPrecondition{};
        const auto expiry=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+std::min(owner_.approval_timeout,std::chrono::milliseconds(std::chrono::hours(1))).count();
        const auto proposal=Json{{"alias",call.tool_name},{"arguments",arguments}}.dump();
        const auto operation=PermissionWaiter(journal_).acquire(operation_id(),{owner_.run_id,owner_.workspace_id,"xlang3_tool",proposal,{}},expiry,cancel,asks?std::string{}:"trusted-xlang3-callable");
        bool dispatched=false;std::string output;bool deferred=false;
        try{
            check();guidance.verify(cancel);
            if(!ready()){deferred=true;output=guidance_required().output_json;}
            else{
                dispatched=true;output=found->second.invoke(call.arguments_json,cancel);
                if(output.size()>1024*1024)throw std::runtime_error("Callable result exceeds limits");(void)Json::parse(output);
            }
        }catch(...){
            try{journal_.finish_operation(operation.id,dispatched?OperationState::uncertain:OperationState::failed,dispatched?R"({"reason":"trusted_callable_effect_uncertain"})":R"({"reason":"trusted_callable_not_dispatched"})").get();}
            catch(...){throw AgentCallableOutcomeUnrecorded("Callable outcome could not be recorded");}
            if(dispatched)throw AgentCallableEffectUncertain("Trusted callable may have produced an effect; it was not retried");throw;
        }
        try{journal_.finish_operation(operation.id,deferred?OperationState::failed:OperationState::succeeded,output).get();}
        catch(...){throw AgentCallableOutcomeUnrecorded("Callable completed but its outcome could not be recorded");}
        return {!deferred,std::move(output)};
    }
private:
    AgentRunJournal& journal_;AgentCallableToolOwnerContext owner_;std::map<std::string,AgentCallableBinding> bindings_;
    std::mutex mutex_;std::set<std::pair<std::string,std::string>> calls_;
};
}
std::unique_ptr<AgentCallableToolHost> make_agent_callable_tool_host(AgentRunJournal& journal,AgentCallableToolOwnerContext owner){return std::make_unique<LocalCallableToolHost>(journal,std::move(owner));}
}
